// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Linq;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Parsers;

namespace FusionUbtPlugin.UHT.Types
{
	public enum FusionRPCTarget
	{
		TargetMasterClient,
		TargetAllClients,
		TargetObjectOwner,
		TargetEveryoneElse
	}
	
	public class FusionRPCFunction : UhtFunction
	{
		public FusionRPCTarget Target { get; set; }
		
		#if UE_5_7_OR_LATER
		public FusionRPCFunction(UhtHeaderFile headerFile, UhtNamespace namespaceObj, UhtType outer, int lineNumber) : base(headerFile, namespaceObj, outer, lineNumber) { }
		#elif UE_5_5_OR_LATER
		public FusionRPCFunction(UhtHeaderFile headerFile, UhtType outer, int lineNumber) : base(headerFile, outer, lineNumber) { }
		#else
		public FusionRPCFunction(UhtHeaderFile headerFile, UhtType outer, int lineNumber) : base(outer, lineNumber) { }
		#endif
		
		protected override bool ResolveSelf(UhtResolvePhase phase)
		{
			switch (phase)
			{
				case UhtResolvePhase.Bases:
					if (FunctionType == UhtFunctionType.Function && Outer is UhtClass outerClass)
					{
						// non-static functions in a const class must be const themselves
						if (outerClass.ClassFlags.HasAnyFlags(EClassFlags.Const))
						{
							FunctionFlags |= EFunctionFlags.Const;
						}
					}
					break;

				case UhtResolvePhase.Properties:
					FusionPropertyParser.ResolveChildren(this, GetPropertyParseOptions(false));
					foreach (UhtProperty property in Properties)
					{
						if (property.DefaultValueTokens != null)
						{
							string key = "CPP_Default_" + property.EngineName;
							if (!MetaData.ContainsKey(key))
							{
								bool parsed = false;
								try
								{
									// All tokens MUST be consumed from the reader
									StringBuilder builder = new();
									
									#if UE_5_6_OR_LATER
									using UhtTokenReplayReaderBorrower borrowedReader = new(property, HeaderFile.Data.Memory, property.DefaultValueTokens.ToArray(), UhtTokenType.EndOfDefault);
									parsed = property.SanitizeDefaultValue(borrowedReader.Reader, builder) && borrowedReader.Reader.IsEOF;
									#else
									IUhtTokenReader defaultValueReader = UhtTokenReplayReader.GetThreadInstance(property, HeaderFile.Data.Memory, property.DefaultValueTokens.ToArray(), UhtTokenType.EndOfDefault);
									parsed = property.SanitizeDefaultValue(defaultValueReader, builder) && defaultValueReader.IsEOF;
									#endif
									
									if (parsed)
									{
										MetaData.Add(key, builder.ToString());
									}
								}
								catch (Exception)
								{
									// Ignore the exception for now
								}

								if (!parsed)
								{
									StringView defaultValueText = new(HeaderFile.Data, property.DefaultValueTokens.First().InputStartPos,
										property.DefaultValueTokens.Last().InputEndPos - property.DefaultValueTokens.First().InputStartPos);
									property.LogError($"C++ Default parameter not parsed: {property.SourceName} '{defaultValueText}'");
								}
							}
						}
					}
					break;

				case UhtResolvePhase.Final:
					if (Outer is UhtClass classObj)
					{
						if (FunctionFlags.HasAnyFlags(EFunctionFlags.Native) &&
							!FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk) &&
							CppImplName != SourceName)
						{
							if (classObj.TryGetDeclaration(CppImplName, out UhtFoundDeclaration declaration))
							{
								FunctionExportFlags |= UhtFunctionExportFlags.ImplFound;
								if (declaration.IsVirtual)
								{
									FunctionExportFlags |= UhtFunctionExportFlags.ImplVirtual;
								}
							}
							if (classObj.TryGetDeclaration(CppValidationImplName, out declaration))
							{
								FunctionExportFlags |= UhtFunctionExportFlags.ValidationImplFound;
								if (declaration.IsVirtual)
								{
									FunctionExportFlags |= UhtFunctionExportFlags.ValidationImplVirtual;
								}
							}
						}

						// If the function has already been marked as blueprint pure, don't bother.  This is important to being
						// able to detect interfaces where the value has been specified.
						if (FunctionType == UhtFunctionType.Function && FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.DeclaredConst) && !FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintPure))
						{
							// @todo: the presence of const and one or more outputs does not guarantee that there are
							// no side effects. On GCC and clang we could use __attribure__((pure)) or __attribute__((const))
							// or we could just rely on the use marking things BlueprintPure. Either way, checking the C++
							// const identifier to determine purity is not desirable. We should remove the following logic:

							// If its a const BlueprintCallable function with some sort of output and is not being marked as an BlueprintPure=false function, mark it as BlueprintPure as well
							if (HasAnyOutputs && FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable) &&
								!FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.ForceBlueprintImpure))
							{
								FunctionFlags |= EFunctionFlags.BlueprintPure;
								FunctionExportFlags |= UhtFunctionExportFlags.AutoBlueprintPure; // Disable error for pure being set
							}
						}
					}

					// The following code is only performed on functions in a class.
					if (Outer is UhtClass)
					{
						foreach (UhtType type in Children)
						{
							if (type is UhtProperty property)
							{
								if (property.PropertyFlags.HasExactFlags(EPropertyFlags.OutParm | EPropertyFlags.ReturnParm, EPropertyFlags.OutParm))
								{
									FunctionFlags |= EFunctionFlags.HasOutParms;
								}
								if (property is UhtStructProperty structProperty)
								{
									if (structProperty.ScriptStruct.HasDefaults)
									{
										FunctionFlags |= EFunctionFlags.HasDefaults;
									}
								}
							}
						}
					}
					break;
			}
			return true;
		}
	}
}