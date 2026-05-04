// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using System.Linq;
using System.Reflection;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Generators;
using FusionUbtPlugin.UHT.Parsers;
using FusionUbtPlugin.UHT.Utils;


namespace FusionUbtPlugin
{
	[UnrealHeaderTool]
	public class FusionUhtCodeGen
	{
		private const string GenerateBodyMacro = "FUSION_BODY";
		public const string HeaderGenSuffix = ".fusion.h";
		public const string CppGenSuffix    = ".fusion.gen.cpp";
		
		private const string HeaderCopyright =
		 "/*===========================================================================\r\n" +
		 "\tGenerated code exported from UnrealHeaderTool.\r\n" +
		 "\tDO NOT modify this manually! Edit the corresponding .h files instead!\r\n" +
		 "===========================================================================*/\r\n" +
		 "\r\n";
		

         public struct PackageInfo
         {
	         public string StrippedName { get; set; }
	         
	         public string Api { get; set; }
         }
         public PackageInfo[] PackageInfos { get; set; }

         public struct HeaderInfo
         {
	         public Task? Task { get; set; }
	         public string IncludePath { get; set; }
	         public string FileId { get; set; }
	         public uint BodyHash { get; set; }
	         public bool NeedsPushModelHeaders { get; set; }
	         public bool NeedsFastArrayHeaders { get; set; }
	         public bool NeedsVerseHeaders { get; set; }
	         public bool NeedsVerseClass { get; set; }
	         public bool NeedsVerseStruct { get; set; }
	         public bool NeedsVerseEnum { get; set; }
	         public bool NeedsVerseInterop { get; set; }
         }
         public HeaderInfo[] HeaderInfos { get; set; }

         public struct ObjectInfo
         {
	         public string RegisteredSingletonName { get; set; }
	         public string UnregisteredSingletonName { get; set; }
	         public string RegisteredExternalDecl { get; set; }
	         public string UnregisteredExternalDecl { get; set; }
	         public UhtClass? NativeInterface { get; set; }
	         public UhtProperty? FastArrayProperty { get; set; }
	         public uint Hash { get; set; }
	         
	         
	         
         }
         public ObjectInfo[] ObjectInfos { get; set; }

         public readonly IUhtExportFactory Factory;
         public UhtSession Session => Factory.Session;
         public UhtScriptStruct? FastArraySerializer { get; set; } = null;
         
         [UhtExporter(Name = "PhotonFusionCodeGen", Description = "Photon Fusion code generation", Options = UhtExporterOptions.Default,
	         ModuleName= "PhotonFusion")]
		private static void ScriptExporter(IUhtExportFactory factory)
		{
			FusionUhtCodeGen gen = new(factory);
			gen.Generate();
		}

		private FusionUhtCodeGen(IUhtExportFactory factory)
		{
			Factory = factory;
			HeaderInfos = new HeaderInfo[Factory.Session.HeaderFileTypeCount];
			ObjectInfos = new ObjectInfo[Factory.Session.ObjectTypeCount];
			PackageInfos = new PackageInfo[Factory.Session.PackageTypeCount];
		}

		private void Generate()
		{
			List<Task?> prereqs = new();

			FastArraySerializer = Session.FindType(null, UhtFindOptions.SourceName | UhtFindOptions.ScriptStruct, "FFastArraySerializer") as UhtScriptStruct;

			// Perform some startup initialization to compute things we need over and over again
			
			#if UE_5_5_OR_LATER
			if (Session.GoWide)
			{
				Parallel.ForEach(Factory.Session.Modules, module =>
				{
					InitModuleInfo(module);
				});
			}
			else
			{
				foreach (UhtModule module in Factory.Session.Modules)
				{
					InitModuleInfo(module);
				}
			}
			#else
			if (Session.GoWide)
			{
				Parallel.ForEach(Factory.Session.Packages, package =>
				{
					InitPackageInfo(package);
				});
			}
			else
			{
				foreach (UhtPackage package in Factory.Session.Packages)
				{
					InitPackageInfo(package);
				}
			}
			#endif
			
			foreach (UhtHeaderFile headerFile in Session.SortedHeaderFiles)
			{
				if (!headerFile.ShouldExport)
				{
					continue;
				}
				
				string? sourceName = null;

				//Find the header files containing fusion functions marked for processing.
				List<FusionRPCFunctionData>? mappedFunctions = null;
				foreach (UhtType child in headerFile.Children)
				{
					if (FusionUhtKeywords.parsedFunctions.TryGetValue(child.SourceName, out mappedFunctions))
					{
						sourceName = child.SourceName;
						break;
					}
				}

				bool hasFunctions = mappedFunctions != null && mappedFunctions.Count > 0;
				
				FusionBodyData bodyData = new(0);
				bool foundBodyData = false;
				foreach (UhtType child in headerFile.Children)
				{
					if (FusionUhtKeywords.bodyData.TryGetValue(child.SourceName, out bodyData))
					{
						sourceName = child.SourceName;
						foundBodyData = true;
						break;
					}
				}
				
				if (!hasFunctions && !foundBodyData)
				{
					// Validate that no property uses FusionArraySize without a .fusion.h being generated
					ValidateNoOrphanedFusionArraySize(headerFile);

					//No work todo for fusion custom UHT...
					continue;
				}

				prereqs.Clear();
				foreach (UhtHeaderFile referenced in headerFile.ReferencedHeadersNoLock)
				{
					if (headerFile != referenced)
					{
						prereqs.Add(HeaderInfos[referenced.HeaderFileTypeIndex].Task);
					}
				}
				
				if (mappedFunctions != null)
				{
					foreach (FusionRPCFunctionData functionData in mappedFunctions)
					{
						UhtPropertyParseOptions options = UhtPropertyParseOptions.DontAddReturn; // Fetch the function name
						options |= UhtPropertyParseOptions.NameIncluded;
						if (functionData.Function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
						{
							options |= UhtPropertyParseOptions.NoAutoConst;
						}

						FusionPropertyParser.ResolveChildren(functionData.Function, options);
						FusionPropertyParser.ResolveChildren(functionData.ReceiveFunction!, options);
					}
				}
				
				HeaderInfos[headerFile.HeaderFileTypeIndex].Task = Factory.CreateTask(prereqs,
					(IUhtExportFactory factory) =>
					{
						new FusionHeaderCodeGeneratorHFile(this, headerFile, mappedFunctions, bodyData).Generate(factory);
						new FusionHeaderCodeGeneratorCppFile(this, headerFile, mappedFunctions).Generate(factory);
					});

				
				FusionUhtKeywords.parsedFunctions.Remove(sourceName!);
				FusionUhtKeywords.bodyData.Remove(sourceName!);
				
				Task? task = HeaderInfos[headerFile.HeaderFileTypeIndex].Task;
				
				Factory.Session.CullOutput = false;
				
				if (task != null)
					task.Wait();
			}
		}
		
#if UE_5_5_OR_LATER
		private void InitModuleInfo(UhtModule module)
		{
			StringBuilder builder = new();

			foreach (UhtPackage package in module.Packages)
			{
				InitPackageInfo(builder, package);
			}
			foreach (UhtHeaderFile headerFile in module.Headers)
			{
				InitHeaderInfo(builder, headerFile);
			}
		}

		private void InitPackageInfo(StringBuilder builder, UhtPackage package)
		{
			ref PackageInfo packageInfo = ref PackageInfos[package.PackageTypeIndex];
			packageInfo.StrippedName = package.SourceName.Replace('/', '_');

			// Construct the names used commonly during export
			ref ObjectInfo objectInfo = ref ObjectInfos[package.ObjectTypeIndex];
			builder.Clear();
			builder.Append("Z_Construct_UPackage_");
			builder.Append(packageInfo.StrippedName);
			objectInfo.UnregisteredSingletonName = objectInfo.RegisteredSingletonName = builder.ToString();
			objectInfo.UnregisteredExternalDecl = objectInfo.RegisteredExternalDecl = $"\tUPackage* {objectInfo.RegisteredSingletonName}();\r\n";
		}
		
		private void InitHeaderInfo(StringBuilder builder, UhtHeaderFile headerFile)
		{
			ref HeaderInfo headerInfo = ref HeaderInfos[headerFile.HeaderFileTypeIndex];

#if UE_5_6_OR_LATER
			// // Find the shortest matching relative path
			// string? relativePath = null;
			// foreach (string includePath in headerFile.Module.Module.IncludePaths)
			// {
			// 	if (headerFile.FilePath.StartsWith(includePath, StringComparison.Ordinal))
			// 	{
			// 		if (relativePath == null || (includePath.Length - headerFile.FilePath.Length - 1) < relativePath.Length)
			// 		{
			// 			relativePath = headerFile.FilePath.Substring(includePath.Length + 1);
			// 		}
			// 	}
			// }
			//
			// // This will create a "../" path which is not great
			// if (relativePath == null)
			// {
			// 	relativePath = Path.GetRelativePath(headerFile.Module.Module.IncludePaths[0], headerFile.FilePath);
			// }
			//
			// // headerInfo.IncludePath = relativePath.Replace('\\', '/');
			// headerInfo.IncludePath = Path.GetRelativePath(relativePath, headerFile.FilePath).Replace('\\', '/');
			
			// Find the shortest matching relative path
			headerInfo.IncludePath = Factory.GetModuleShortestIncludePath(headerFile.Module, headerFile.FilePath);
#else
			headerInfo.IncludePath = Path.GetRelativePath(headerFile.Module.Module.IncludeBase, headerFile.FilePath).Replace('\\', '/');
#endif
			// Convert the file path to a C identifier
			string filePath = headerFile.FilePath;
			bool isRelative = !Path.IsPathRooted(filePath);
			if (!isRelative && Session.EngineDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.EngineDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			if (!isRelative && Session.ProjectDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.ProjectDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			filePath = filePath.Replace('\\', '/');
			if (isRelative)
			{
				while (filePath.StartsWith("../", StringComparison.Ordinal))
				{
					filePath = filePath[3..];
				}
			}

			char[] outFilePath = new char[filePath.Length + 4];
			outFilePath[0] = 'F';
			outFilePath[1] = 'I';
			outFilePath[2] = 'D';
			outFilePath[3] = '_';
			for (int index = 0; index < filePath.Length; ++index)
			{
				outFilePath[index + 4] = UhtFCString.IsAlnum(filePath[index]) ? filePath[index] : '_';
			}
			headerInfo.FileId = new string(outFilePath);

			foreach (UhtType headerFileChild in headerFile.Children)
			{
				if (headerFileChild is UhtObject obj)
				{
					UhtPackage? package = obj.Outer as UhtPackage;
					if (package == null)
					{
						throw new UhtIceException("Expected type defined in a header to have a package outer");
					}
					InitObjectInfo(builder, package, ref headerInfo, obj);
				}
			}

			headerInfo.NeedsVerseInterop = headerInfo.NeedsVerseStruct;
		}
		
		private void InitObjectInfo(StringBuilder builder, UhtPackage package, ref HeaderInfo headerInfo, UhtObject obj)
		{
			UhtModule module = package.Module;
			ref ObjectInfo objectInfo = ref ObjectInfos[obj.ObjectTypeIndex];

			builder.Clear();

			// Construct the names used commonly during export
			bool isNonIntrinsicClass = false;
			builder.Append("Z_Construct_U").Append(obj.EngineClassName).AppendOuterNames(obj);

			string engineClassName = obj.EngineClassName;
			if (obj is UhtClass classObj)
			{
				if (!classObj.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
				{
					isNonIntrinsicClass = true;
				}
				if (classObj.ClassExportFlags.HasExactFlags(UhtClassExportFlags.HasReplciatedProperties, UhtClassExportFlags.SelfHasReplicatedProperties))
				{
					headerInfo.NeedsPushModelHeaders = true;
				}
				if (classObj.IsVerseField)
				{
					headerInfo.NeedsVerseClass = true;
				}
				if (classObj.Children.Any(x => x is UhtVerseValueProperty))
				{
					headerInfo.NeedsVerseHeaders = true;
				}
				if (classObj.ClassType == UhtClassType.NativeInterface)
				{
					if (classObj.AlternateObject != null)
					{
						ObjectInfos[classObj.AlternateObject.ObjectTypeIndex].NativeInterface = classObj;
					}
				}
			}
			else if (obj is UhtScriptStruct scriptStructObj)
			{
				if (scriptStructObj.IsVerseField)
				{
					headerInfo.NeedsVerseStruct = true;
				}
				if (scriptStructObj.Children.Any(x => x is UhtVerseValueProperty))
				{
					headerInfo.NeedsVerseHeaders = true;
				}

				// Check to see if we are a FastArraySerializer and should try to deduce the FastArraySerializerItemType
				// To fulfill that requirement the struct should be derived from FFastArraySerializer and have a single replicated TArrayProperty
				if (scriptStructObj.IsChildOf(FastArraySerializer))
				{
					// If Super is a valid fastarray we mark this struct as a FastArrayProperty as well
					if (scriptStructObj.Super != null && ObjectInfos[scriptStructObj.Super.ObjectTypeIndex].FastArrayProperty != null)
					{
						objectInfo.FastArrayProperty = ObjectInfos[scriptStructObj.Super.ObjectTypeIndex].FastArrayProperty;
					}

					// A valid fastarray cannot have any additional replicated properties.
					foreach (UhtType child in scriptStructObj.Children)
					{
						if (child is UhtProperty property)
						{
							if (!property.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip) && property is UhtArrayProperty)
							{
								if (objectInfo.FastArrayProperty != null)
								{
									objectInfo.FastArrayProperty = null;
									break;
								}
								objectInfo.FastArrayProperty = property;
							}
						}
					}
					if (objectInfo.FastArrayProperty != null)
					{
						headerInfo.NeedsFastArrayHeaders = true;
					}
				}
			}
			else if (obj is UhtFunction)
			{
				// The method for EngineClassName returns type specific where in this case we need just the simple return type
				engineClassName = "Function";
			}
			else if (obj is UhtEnum enumObj)
			{
				if (enumObj.IsVerseField)
				{
					headerInfo.NeedsVerseEnum = true;
				}
			}

			if (isNonIntrinsicClass)
			{
				objectInfo.RegisteredSingletonName = builder.ToString();
				builder.Append("_NoRegister");
				objectInfo.UnregisteredSingletonName = builder.ToString();

				objectInfo.UnregisteredExternalDecl = $"\t{module.Api}U{engineClassName}* {objectInfo.UnregisteredSingletonName}();\r\n";
				objectInfo.RegisteredExternalDecl = $"\t{module.Api}U{engineClassName}* {objectInfo.RegisteredSingletonName}();\r\n";
			}
			else
			{
				objectInfo.UnregisteredSingletonName = objectInfo.RegisteredSingletonName = builder.ToString();
				objectInfo.UnregisteredExternalDecl = objectInfo.RegisteredExternalDecl = $"\t{module.Api}U{engineClassName}* {objectInfo.RegisteredSingletonName}();\r\n";
			}

			// Init the children
			foreach (UhtType child in obj.Children)
			{
				if (child is UhtObject childObject)
				{
					InitObjectInfo(builder, package, ref headerInfo, childObject);
				}
			}
		}
#else
		private void InitPackageInfo(UhtPackage package)
		{
			StringBuilder builder = new();

			ref PackageInfo packageInfo = ref PackageInfos[package.PackageTypeIndex];
			packageInfo.StrippedName = package.SourceName.Replace('/', '_');
			packageInfo.Api = $"{package.ShortName.ToString().ToUpper()}_API ";

			// Construct the names used commonly during export
			ref ObjectInfo objectInfo = ref ObjectInfos[package.ObjectTypeIndex];
			builder.Append("Z_Construct_UPackage_");
			builder.Append(packageInfo.StrippedName);
			objectInfo.UnregisteredSingletonName = objectInfo.RegisteredSingletonName = builder.ToString();
			objectInfo.UnregisteredExternalDecl = objectInfo.RegisteredExternalDecl = $"\tUPackage* {objectInfo.RegisteredSingletonName}();\r\n";

			foreach (UhtType packageChild in package.Children)
			{
				if (packageChild is UhtHeaderFile headerFile)
				{
					InitHeaderInfo(builder, package, ref packageInfo, headerFile);
				}
			}
		}
		
		private void InitHeaderInfo(StringBuilder builder, UhtPackage package, ref PackageInfo packageInfo, UhtHeaderFile headerFile)
		{
			ref HeaderInfo headerInfo = ref HeaderInfos[headerFile.HeaderFileTypeIndex];

			headerInfo.IncludePath = Path.GetRelativePath(package.Module.IncludeBase, headerFile.FilePath).Replace('\\', '/');

			// Convert the file path to a C identifier
			string filePath = headerFile.FilePath;
			bool isRelative = !Path.IsPathRooted(filePath);
			if (!isRelative && Session.EngineDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.EngineDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			if (!isRelative && Session.ProjectDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.ProjectDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			filePath = filePath.Replace('\\', '/');
			if (isRelative)
			{
				while (filePath.StartsWith("../", StringComparison.Ordinal))
				{
					filePath = filePath[3..];
				}
			}

			char[] outFilePath = new char[filePath.Length + 4];
			outFilePath[0] = 'F';
			outFilePath[1] = 'I';
			outFilePath[2] = 'D';
			outFilePath[3] = '_';
			for (int index = 0; index < filePath.Length; ++index)
			{
				outFilePath[index + 4] = UhtFCString.IsAlnum(filePath[index]) ? filePath[index] : '_';
			}
			headerInfo.FileId = new string(outFilePath);

			foreach (UhtType headerFileChild in headerFile.Children)
			{
				if (headerFileChild is UhtObject obj)
				{
					InitObjectInfo(builder, package, ref packageInfo, ref headerInfo, obj);
				}
			}
		}
		
		private void InitObjectInfo(StringBuilder builder, UhtPackage package, ref PackageInfo packageInfo, ref HeaderInfo headerInfo, UhtObject obj)
		{
			ref ObjectInfo objectInfo = ref ObjectInfos[obj.ObjectTypeIndex];

			builder.Clear();

			// Construct the names used commonly during export
			bool isNonIntrinsicClass = false;
			builder.Append("Z_Construct_U").Append(obj.EngineClassName).AppendOuterNames(obj);

			string engineClassName = obj.EngineClassName;
			if (obj is UhtClass classObj)
			{
				if (!classObj.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
				{
					isNonIntrinsicClass = true;
				}
				if (classObj.ClassExportFlags.HasExactFlags(UhtClassExportFlags.HasReplciatedProperties, UhtClassExportFlags.SelfHasReplicatedProperties))
				{
					headerInfo.NeedsPushModelHeaders = true;
				}
				if (classObj.ClassType == UhtClassType.NativeInterface)
				{
					if (classObj.AlternateObject != null)
					{
						ObjectInfos[classObj.AlternateObject.ObjectTypeIndex].NativeInterface = classObj;
					}
				}
				headerInfo.NeedsVerseHeaders = classObj.Children.Any(x => x is UhtVerseValueProperty);
			}
			else if (obj is UhtScriptStruct scriptStruct)
			{
				// Check to see if we are a FastArraySerializer and should try to deduce the FastArraySerializerItemType
				// To fulfill that requirement the struct should be derived from FFastArraySerializer and have a single replicated TArrayProperty
				if (scriptStruct.IsChildOf(FastArraySerializer))
				{
					foreach (UhtType child in scriptStruct.Children)
					{
						if (child is UhtProperty property)
						{
							if (!property.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip) && property is UhtArrayProperty)
							{
								if (objectInfo.FastArrayProperty != null)
								{
									objectInfo.FastArrayProperty = null;
									break;
								}
								objectInfo.FastArrayProperty = property;
							}
						}
					}
					if (objectInfo.FastArrayProperty != null)
					{
						headerInfo.NeedsFastArrayHeaders = true;
					}
				}
				headerInfo.NeedsVerseHeaders = scriptStruct.Children.Any(x => x is UhtVerseValueProperty);
			}
			else if (obj is UhtFunction)
			{
				// The method for EngineClassName returns type specific where in this case we need just the simple return type
				engineClassName = "Function";
			}

			if (isNonIntrinsicClass)
			{
				objectInfo.RegisteredSingletonName = builder.ToString();
				builder.Append("_NoRegister");
				objectInfo.UnregisteredSingletonName = builder.ToString();

				objectInfo.UnregisteredExternalDecl = $"\t{packageInfo.Api}U{engineClassName}* {objectInfo.UnregisteredSingletonName}();\r\n";
				objectInfo.RegisteredExternalDecl = $"\t{packageInfo.Api}U{engineClassName}* {objectInfo.RegisteredSingletonName}();\r\n";
			}
			else
			{
				objectInfo.UnregisteredSingletonName = objectInfo.RegisteredSingletonName = builder.ToString();
				objectInfo.UnregisteredExternalDecl = objectInfo.RegisteredExternalDecl = $"\t{packageInfo.Api}U{engineClassName}* {objectInfo.RegisteredSingletonName}();\r\n";
			}

			// Init the children
			foreach (UhtType child in obj.Children)
			{
				if (child is UhtObject childObject)
				{
					InitObjectInfo(builder, package, ref packageInfo, ref headerInfo, childObject);
				}
			}
		}
#endif
		
		#region Validation

		private static void ValidateNoOrphanedFusionArraySize(UhtHeaderFile headerFile)
		{
			foreach (UhtType child in headerFile.Children)
			{
				if (child is UhtStruct structObj)
				{
					foreach (UhtType member in structObj.Children)
					{
						if (member is UhtProperty property && property.MetaData.ContainsKey("FusionArraySize"))
						{
							throw new UhtException(property,
								$"Property '{property.SourceName}' in '{structObj.SourceName}' uses FusionArraySize but the header does not include a corresponding .fusion.h file. " +
								$"Add 'FUSION_BODY();' to '{structObj.SourceName}' and '#include \"{headerFile.FileNameWithoutExtension}.fusion.h\"' to the header file. And also FUSION_BODY(); macro below GENERATED_BODY()");
						}
					}
				}
			}
		}

		#endregion

		#region Utility functions
		/// <summary>
		/// Return the singleton name for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="registered">If true, return the registered singleton name.  Otherwise return the unregistered.</param>
		/// <returns>Singleton name or "nullptr" if Object is null</returns>
		public string GetSingletonName(UhtObject? obj, bool registered)
		{
			if (obj == null)
			{
				return "nullptr";
			}
			return registered ? ObjectInfos[obj.ObjectTypeIndex].RegisteredSingletonName : ObjectInfos[obj.ObjectTypeIndex].UnregisteredSingletonName;
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="registered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(UhtObject obj, bool registered)
		{
			return GetExternalDecl(obj.ObjectTypeIndex, registered);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="objectIndex">The object in question.</param>
		/// <param name="registered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(int objectIndex, bool registered)
		{
			return registered ? ObjectInfos[objectIndex].RegisteredExternalDecl : ObjectInfos[objectIndex].UnregisteredExternalDecl;
		}
		#endregion
	}
}
