// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Utils;
using BorrowStringBuilder = EpicGames.UHT.Utils.BorrowStringBuilder;
using StringBuilderCache = EpicGames.UHT.Utils.StringBuilderCache;

namespace FusionUbtPlugin.UHT.Generators
{
	public class FusionHeaderCodeGeneratorHFile : FusionCodeGenerator
	{
		private FusionBodyData bodyData;
		
		public FusionHeaderCodeGeneratorHFile(FusionUhtCodeGen codeGen, UhtHeaderFile headerFile, List<FusionRPCFunctionData>? parsedFunctions, FusionBodyData bodyData)
			: base(codeGen, headerFile, parsedFunctions)
		{
			this.bodyData = bodyData;
		}

		public void Generate(IUhtExportFactory factory)
		{
			ref FusionUhtCodeGen.HeaderInfo headerInfo = ref HeaderInfos[HeaderFile.HeaderFileTypeIndex];
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Big);
				StringBuilder builder = borrower.StringBuilder;

				builder.Append(HeaderCopyright);
				builder.Append("// IWYU pragma: private, include \"").Append(HeaderFile.IncludeFilePath)
					.Append("\"\r\n");

				// Attempt to limit the headers included. This is needed in the lower level engine code
				// to get around circular header include issues.
				{
					if (HeaderFile.References.ExportTypes.Count > 0 &&
					    HeaderFile.References.ExportTypes.Find(x => x is not UhtEnum) == null)
					{
						builder.Append("#include \"Templates/IsUEnumClass.h\"\r\n");
						builder.Append("#include \"UObject/ObjectMacros.h\"\r\n");
						builder.Append("#include \"UObject/ReflectedTypeAccessors.h\"\r\n");
					}
					else
					{
						builder.Append("#include \"UObject/ObjectMacros.h\"\r\n");
						builder.Append("#include \"UObject/ScriptMacros.h\"\r\n");
					}
				}

				builder.Append("\r\n");
				builder.Append(DisableDeprecationWarnings).Append("\r\n");
				
				string strippedName = Path.GetFileNameWithoutExtension(HeaderFile.FilePath);
				
				#if UE_5_5_OR_LATER
				string defineName = $"{HeaderFile.Module.ShortName.ToString().ToUpper()}_{strippedName}_fusion_h";
				#else
				string defineName = $"{HeaderFile.Package.ShortName.ToString().ToUpper()}_{strippedName}_fusion_h";
				#endif

				#if UE_5_7_OR_LATER
				StringComparerUE compareUE = StringComparerUE.OrdinalIgnoreCase;
				if (HeaderFile.References.ForwardDeclarations.Count > 0)
				{
					HashSet<UhtField> forwardDeclarations = HeaderFile.References.ForwardDeclarations;
					int index1 = 0;
					UhtField[] uhtFieldArray = new UhtField[forwardDeclarations.Count];
					foreach (UhtField uhtField in forwardDeclarations)
					{
						uhtFieldArray[index1] = uhtField;
						++index1;
					}
					UhtField[] sorted = uhtFieldArray;
					Array.Sort<UhtField>(sorted, (Comparison<UhtField>) ((x, y) =>
					{
						int num1 = compareUE.Compare(x.Namespace.FullSourceName, y.Namespace.FullSourceName);
						if (num1 != 0)
							return num1;
						int num2 = compareUE.Compare(x.EngineClassName, y.EngineClassName);
						return num2 != 0 ? num2 : compareUE.Compare(x.SourceName, y.SourceName);
					}));
					int index = 0;
					while (index < sorted.Length)
					{
						UhtNamespace namespaceObj = sorted[index].Namespace;
						namespaceObj.AppendMultipleLines(builder, (Action<StringBuilder>) (b =>
						{
							for (; index < sorted.Length && sorted[index].Namespace == namespaceObj; ++index)
								b.AppendForwardDeclaration(sorted[index]).Append("\r\n");
						}));
					}
				}
				#else
				if (HeaderFile.References.ForwardDeclarations.Count > 0)
				{
					string[] sorted = new string[HeaderFile.References.ForwardDeclarations.Count];
					int index = 0;
					foreach (string forwardDeclaration in HeaderFile.References.ForwardDeclarations)
					{
						sorted[index++] = forwardDeclaration;
					}
					Array.Sort(sorted, StringComparerUE.OrdinalIgnoreCase);
					foreach (string forwardDeclaration in sorted)
					{
						builder.Append(forwardDeclaration).Append("\r\n");
					}
				}
				#endif

				builder.Append("#ifdef ").Append(defineName).Append("\r\n");
				builder.Append("#error \"").Append(strippedName)
					.Append(".fusion.h already included, missing '#pragma once' in ").Append(strippedName)
					.Append(".h\"\r\n");
				builder.Append("#endif\r\n");
				builder.Append("#define ").Append(defineName).Append("\r\n");
				builder.Append("\r\n");

				foreach (UhtField field in HeaderFile.References.ExportTypes)
				{
					if (field is UhtClass)
					{
						using (FusionMacroCreator macro = new(builder, this, bodyData.MacroLineNumber, "FUSION_BODY"))
						{
							if (parsedFunctions != null)
							{
								foreach (FusionRPCFunctionData functionData in parsedFunctions)
								{
									if (functionData.ReceiveFunction == null)
									{
										throw new Exception("Receive function cannot be null!");
									}
									builder.Append($"\tUPROPERTY() bool __FUSIONRPCEVENT_{functionData.ReceiveFunction.CppImplName};").Append("\\\r\n");
								}
							}
							
							//Terminate macro.
							builder.Append(" \\\r\n");
						}
					}
				}

				builder.Append("#undef CURRENT_FILE_ID\r\n");
				builder.Append("#define CURRENT_FILE_ID ").Append(headerInfo.FileId).Append("\r\n\r\n\r\n");

				builder.Append(EnableDeprecationWarnings).Append("\r\n");

				if (SaveExportedHeaders)
				{
					string headerFilePath = MakeExportPath(HeaderFile, ".fusion.h");
					factory.CommitOutput(headerFilePath, builder);
				}
			}
		}
	}
}