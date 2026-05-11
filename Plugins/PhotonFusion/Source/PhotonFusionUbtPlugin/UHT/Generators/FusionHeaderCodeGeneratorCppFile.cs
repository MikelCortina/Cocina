// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Types;
using FusionUbtPlugin.UHT.Utils;
using BorrowStringBuilder = FusionUbtPlugin.UHT.Utils.BorrowStringBuilder;
using StringBuilderCache = FusionUbtPlugin.UHT.Utils.StringBuilderCache;

namespace FusionUbtPlugin.UHT.Generators
{
	public class FusionHeaderCodeGeneratorCppFile : FusionCodeGenerator
	{

		public FusionHeaderCodeGeneratorCppFile(FusionUhtCodeGen codeGen, UhtHeaderFile headerFile, List<FusionRPCFunctionData>? parsedFunctions)
			: base(codeGen, headerFile, parsedFunctions)
		{
		}
		
		public void Generate(IUhtExportFactory factory)
		{
			ref FusionUhtCodeGen.HeaderInfo headerInfo = ref HeaderInfos[HeaderFile.HeaderFileTypeIndex];
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Big);
				StringBuilder builder = borrower.StringBuilder;

				builder.Append(HeaderCopyright);
				builder.Append(RequiredCPPIncludes);
				builder.Append("#include \"").Append(headerInfo.IncludePath).Append("\"\r\n");

				int generatedBodyStart = builder.Length;

				// Generate FusionArraySize registrations for packaged builds
				GenerateArraySizeRegistrations(builder);

				if (parsedFunctions != null)
				{
					//Hardcoded for now, move into const lookup thingy later.
					builder.Append("#include \"").Append("FusionOnlineSubsystem.h").Append("\"\r\n");
					builder.Append("#include \"").Append("FusionHelpers.h").Append("\"\r\n");
					builder.Append("#include \"").Append("Types/FusionTypeDescriptor.h").Append("\"\r\n");
					
					builder.Append("#include \"").Append("Kismet/GameplayStatics.h").Append("\"\r\n");
					builder.Append("#include \"").Append("Engine/GameInstance.h").Append("\"\r\n");

					HashSet<UhtHeaderFile> addedIncludes = new();
					List<string> includesToAdd = new();
					addedIncludes.Add(HeaderFile);

					foreach (FusionRPCFunctionData functionData in parsedFunctions)
					{
						foreach (UhtProperty property in functionData.Function.Properties)
						{
							AddIncludeForProperty(property, true, addedIncludes, includesToAdd);
						}
					}
				
					foreach (string include in includesToAdd)
					{
						builder.Append("#include \"").Append(include).Append("\"\r\n");
					}
				
					foreach (FusionRPCFunctionData functionData in parsedFunctions)
					{
						DefineNativeFunction(builder, functionData, UhtPropertyTextType.ClassFunctionArgOrRetVal, false, null, null, UhtFunctionExportFlags.None, 0, "\r\n");
						DefineNativeFunctionBody(builder, functionData);
					}
				}
				
				//Write output
				{
					using UhtRentedPoolBuffer<char> borrowBuffer = builder.RentPoolBuffer();
					string cppFilePath = MakeExportPath(HeaderFile, ".fusion.gen.cpp");
					StringView generatedBody = new(borrowBuffer.Buffer.Memory);
					if (SaveExportedHeaders)
					{
						factory.CommitOutput(cppFilePath, generatedBody);
					}
					
					int generatedBodyEnd = builder.Length;
					
					// Save the hash of the generated body 
					HeaderInfos[HeaderFile.HeaderFileTypeIndex].BodyHash = UhtHash.GenenerateTextHash(generatedBody.Span[generatedBodyStart..generatedBodyEnd]);
				}
			}
		}
		

		
		private void GenerateArraySizeRegistrations(StringBuilder builder)
		{
			List<(string ownerName, string propertyName, string size)> registrations = new();

			foreach (UhtType child in HeaderFile.Children)
			{
				if (child is UhtClass classObj)
				{
					CollectArraySizeMetadata(classObj, registrations);
				}
				else if (child is UhtScriptStruct structObj)
				{
					CollectArraySizeMetadata(structObj, registrations);
				}
			}

			if (registrations.Count > 0)
			{
				builder.Append("#include \"Types/FusionPropertyHelpers.h\"\r\n");
				builder.Append("static struct FFusionArraySizeRegistrar_").Append(Path.GetFileNameWithoutExtension(HeaderFile.FilePath)).Append(" {\r\n");
				builder.Append("\tFFusionArraySizeRegistrar_").Append(Path.GetFileNameWithoutExtension(HeaderFile.FilePath)).Append("() {\r\n");
				foreach (var (ownerName, propertyName, size) in registrations)
				{
					builder.Append("\t\tFusionMeta::RegisterArraySize(FName(TEXT(\"").Append(ownerName).Append("\")), FName(TEXT(\"").Append(propertyName).Append("\")), ").Append(size).Append(");\r\n");
				}
				builder.Append("\t}\r\n");
				builder.Append("} GFusionArraySizeRegistrar_").Append(Path.GetFileNameWithoutExtension(HeaderFile.FilePath)).Append(";\r\n\r\n");
			}
		}

		private static void CollectArraySizeMetadata(UhtStruct structObj, List<(string ownerName, string propertyName, string size)> registrations)
		{
			foreach (UhtType member in structObj.Children)
			{
				if (member is UhtProperty property && property.MetaData.TryGetValue("FusionArraySize", out string? sizeValue))
				{
					registrations.Add((structObj.EngineName, property.EngineName, sizeValue));
				}
			}
		}

		private void AddIncludeForProperty(UhtProperty property, bool requireIncludeForClasses, HashSet<UhtHeaderFile> addedIncludes, IList<string> includesToAdd)
		{
			AddIncludeForType(property, requireIncludeForClasses, addedIncludes, includesToAdd);

			if (property is UhtContainerBaseProperty containerProperty)
			{
				AddIncludeForType(containerProperty.ValueProperty, false, addedIncludes, includesToAdd);
			}

			if (property is UhtMapProperty mapProperty)
			{
				AddIncludeForType(mapProperty.KeyProperty, false, addedIncludes, includesToAdd);
			}
		}
		
		private void AddIncludeForType(UhtProperty uhtProperty, bool requireIncludeForClasses, HashSet<UhtHeaderFile> addedIncludes, IList<string> includesToAdd)
		{
			if (uhtProperty is UhtStructProperty structProperty)
			{
				UhtScriptStruct scriptStruct = structProperty.ScriptStruct;
				if (!scriptStruct.HeaderFile.IsNoExportTypes && addedIncludes.Add(scriptStruct.HeaderFile))
				{
					includesToAdd.Add(HeaderInfos[scriptStruct.HeaderFile.HeaderFileTypeIndex].IncludePath);
				}
			}
			else if (requireIncludeForClasses && uhtProperty is UhtClassProperty classProperty)
			{
				UhtClass uhtClass = classProperty.Class;
				if (!uhtClass.HeaderFile.IsNoExportTypes && addedIncludes.Add(uhtClass.HeaderFile))
				{
					includesToAdd.Add(HeaderInfos[uhtClass.HeaderFile.HeaderFileTypeIndex].IncludePath);
				}
			}
		}

		protected static StringBuilder DefineNativeFunction(StringBuilder builder, FusionRPCFunctionData functionData,
			UhtPropertyTextType textType, bool isDeclaration,
			string? alternateFunctionName, string? extraParam, UhtFunctionExportFlags extraExportFlags, int tabs,
			string endl)
		{
			UhtClass? outerClass = functionData.Function.Outer as UhtClass;
			UhtFunctionExportFlags exportFlags = functionData.Function.FunctionExportFlags | extraExportFlags;
			bool isDelegate = functionData.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate);
			bool isInterface = !isDelegate && (outerClass != null && outerClass.ClassFlags.HasAnyFlags(EClassFlags.Interface));
			bool isK2Override = functionData.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent);

			builder.AppendTabs(tabs);

			if (isDeclaration)
			{
				// If the function was marked as 'RequiredAPI', then add the *_API macro prefix.  Note that if the class itself
				// was marked 'RequiredAPI', this is not needed as C++ will exports all methods automatically.
				
				//TODO: Fix this later /Linus
				// if (textType != UhtPropertyTextType.EventFunctionArgOrRetVal &&
				// 	!(outerClass != null && outerClass.ClassFlags.HasAnyFlags(EClassFlags.RequiredAPI)) &&
				// 	exportFlags.HasAnyFlags(UhtFunctionExportFlags.RequiredAPI))
				// {
				// 	builder.Append(Module.Api);
				// }

				if (textType == UhtPropertyTextType.InterfaceFunctionArgOrRetVal)
				{
					builder.Append("static ");
				}
				else if (isK2Override)
				{
					builder.Append("virtual ");
				}
				// if the owning class is an interface class
				else if (isInterface)
				{
					builder.Append("virtual ");
				}
				// this is not an event, the function is not a static function and the function is not marked final
				else if (textType != UhtPropertyTextType.EventFunctionArgOrRetVal && !functionData.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Static) && !exportFlags.HasAnyFlags(UhtFunctionExportFlags.Final))
				{
					builder.Append("virtual ");
				}
				else if (exportFlags.HasAnyFlags(UhtFunctionExportFlags.Inline))
				{
					builder.Append("inline ");
				}
			}

			UhtProperty? returnProperty = functionData.Function.ReturnProperty;
			if (returnProperty != null)
			{
				if (returnProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
				{
					builder.Append("const ");
				}
				builder.AppendPropertyText(returnProperty, textType);
			}
			else
			{
				builder.Append("void");
			}

			builder.Append(' ');
			if (!isDeclaration && outerClass != null)
			{
				builder.AppendClassSourceNameOrInterfaceName(outerClass).Append("::");
			}

			if (alternateFunctionName != null)
			{
				builder.Append(alternateFunctionName);
			}
			else
			{
				switch (textType)
				{
					case UhtPropertyTextType.InterfaceFunctionArgOrRetVal:
						builder.Append("Execute_").Append(functionData.Function.SourceName);
						break;
					case UhtPropertyTextType.EventFunctionArgOrRetVal:
						builder.Append(functionData.Function.MarshalAndCallName);
						break;
					case UhtPropertyTextType.ClassFunctionArgOrRetVal:
						builder.Append(functionData.Function.CppImplName);
						break;
					default:
						throw new UhtIceException("Unexpected type text");
				}
			}

			AppendParameters(builder, functionData.Function, textType, extraParam, false);

			if (textType != UhtPropertyTextType.InterfaceFunctionArgOrRetVal)
			{
				if (!isDelegate && functionData.Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const))
				{
					builder.Append(" const");
				}

				if (isInterface && isDeclaration)
				{
					// all methods in interface classes are pure virtuals
					if (isK2Override)
					{
						// For BlueprintNativeEvent methods we emit a stub implementation. This allows Blueprints that implement the interface class to be nativized.
						builder.Append(" {");
						if (returnProperty != null)
						{
							if (returnProperty is UhtByteProperty byteProperty && byteProperty.Enum != null)
							{
								builder.Append(" return TEnumAsByte<").Append(byteProperty.Enum.CppType).Append(">(").AppendNullConstructorArg(returnProperty, false).Append("); ");
							}
							else if (returnProperty is UhtEnumProperty enumProperty && enumProperty.Enum.CppForm != UhtEnumCppForm.EnumClass)
							{
								builder.Append(" return TEnumAsByte<").Append(enumProperty.Enum.CppType).Append(">(").AppendNullConstructorArg(returnProperty, false).Append("); ");
							}
							else
							{
								builder.Append(" return ").AppendNullConstructorArg(returnProperty, false).Append("; ");
							}
						}
						builder.Append('}');
					}
					else
					{
						builder.Append("=0");
					}
				}
			}
			builder.Append(endl);

			return builder;
		}

		protected static StringBuilder DefineNativeFunctionBody(StringBuilder builder, FusionRPCFunctionData functionData)
		{
			if (functionData.ReceiveFunction == null)
			{
				throw new Exception("Receive function cannot be null!");
			}
			
			builder.Append("{\r\n");
			
			builder.Append("auto World = GetWorld(); \r\n");
			builder.Append("auto GameInstance = UGameplayStatics::GetGameInstance(World); \r\n");
			builder.Append("auto OnlineSubsystem = GameInstance->GetSubsystem<UFusionOnlineSubsystem>(); \r\n");
			
			//Move this somewhere else later, it should match what we do with our blueprint supported RPCs (see FusionRPCFunctionNode)
			string nameString = functionData.Function.SourceName;
			uint hash = FusionUhtCrc32.Compute(nameString);
			int hashInt32 = (int)(hash & 0x7FFFFFFF);
			
			builder.Append("TArray<uint8> Buffer; \r\n");
            builder.Append($"UFusionFunctionDescriptor* Descriptor = UFusionHelpers::GetFunctionDescriptor(this, \"{functionData.ReceiveFunction.CppImplName}\"); \r\n");
            builder.Append("if (!Descriptor)\r\n");
            builder.Append("{\r\n");
            builder.Append("\tUE_LOG(LogTemp, Warning, TEXT(\"Missing Function Descriptor for Fusion RPC in '%s'. Make sure the Actor is set to replicate.\"), *GetName());\r\n");
            builder.Append("\treturn;\r\n");
            builder.Append("}\r\n");

            //Run through all properties of the function and serialize them into Buffer.
            int propertyIndex = 0;
			foreach (UhtType parameter in functionData.Function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					if (property.PropertyFlags.HasFlag(EPropertyFlags.ReferenceParm))
					{
						builder.Append($"Descriptor->SerializeParams({parameter.SourceName}, {propertyIndex++}, Buffer, this); \r\n");
					}
					else
					{
						builder.Append($"Descriptor->SerializeParams(&{parameter.SourceName}, {propertyIndex++}, Buffer, this); \r\n");
					}
				}
				else
				{
					builder.Append($"Descriptor->SerializeParams(&{parameter.SourceName}, {propertyIndex++}, Buffer, this); \r\n");
				}

			}

			string targetString;
			switch (functionData.Function.Target)
			{
				case FusionRPCTarget.TargetAllClients:
					targetString = "EFusionRPCTarget::SendToAllClients";
					break;
				case FusionRPCTarget.TargetMasterClient:
					targetString = "EFusionRPCTarget::SendToMasterClient";
					break;
				case FusionRPCTarget.TargetObjectOwner:
					targetString = "EFusionRPCTarget::SendToObjectOwner";
					break;
				case FusionRPCTarget.TargetEveryoneElse:
					targetString = "EFusionRPCTarget::SendToEveryoneElse";
					break;
				default:
					throw new UhtIceException("Unexpected target type");
			}
			
			
			builder.Append($"OnlineSubsystem->SendCustomRPC(this, \"{functionData.ReceiveFunction.CppImplName}\", {hashInt32}, {targetString}, Buffer, ERPCMode::FusionRPC); \r\n");
			
			builder.Append("}\r\n");

			return builder;
		}
		
		protected static StringBuilder AppendParameters(StringBuilder builder, UhtFunction function, UhtPropertyTextType textType, string? extraParameter, bool skipParameterName)
		{
			bool needsSeperator = false;

			builder.Append('(');

			if (extraParameter != null)
			{
				builder.Append(extraParameter);
				needsSeperator = true;
			}

			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					if (needsSeperator)
					{
						builder.Append(", ");
					}
					else
					{
						needsSeperator = true;
					}
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						builder.Append("const ");
					}
					property.AppendText(builder, UhtPropertyTextType.GenericFunctionArgOrRetVal);
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.ReferenceParm | EPropertyFlags.OutParm))
					{
						builder.Append('&');
					}
					if (!skipParameterName)
					{
						builder.Append(' ').Append(property.SourceName);
					}
				}
			}

			builder.Append(')');
			return builder;
		}
	}
	
	
	

}