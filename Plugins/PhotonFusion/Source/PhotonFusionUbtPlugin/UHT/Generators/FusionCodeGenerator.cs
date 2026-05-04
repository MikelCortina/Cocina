// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System.Collections.Generic;
using System.IO;
using EpicGames.UHT.Types;

namespace FusionUbtPlugin.UHT.Generators
{
	public class FusionCodeGenerator : FusionPackageCodeGenerator
	{
		#region Define block macro names

		public const string EventParamsMacroSuffix = "EVENT_PARMS";
		public const string CallbackWrappersMacroSuffix = "CALLBACK_WRAPPERS";
		public const string SparseDataMacroSuffix = "SPARSE_DATA";
		public const string SparseDataPropertyAccessorsMacroSuffix = "SPARSE_DATA_PROPERTY_ACCESSORS";
		public const string RpcWrappersMacroSuffix = "RPC_WRAPPERS";
		public const string RpcWrappersNoPureDeclsMacroSuffix = "RPC_WRAPPERS_NO_PURE_DECLS";
		public const string AccessorsMacroSuffix = "ACCESSORS";
		public const string ArchiveSerializerMacroSuffix = "ARCHIVESERIALIZER";
		public const string StandardConstructorsMacroSuffix = "STANDARD_CONSTRUCTORS";
		public const string EnchancedConstructorsMacroSuffix = "ENHANCED_CONSTRUCTORS";
		public const string GeneratedBodyMacroSuffix = "GENERATED_BODY";
		public const string GeneratedBodyLegacyMacroSuffix = "GENERATED_BODY_LEGACY";
		public const string GeneratedUInterfaceBodyMacroSuffix = "GENERATED_UINTERFACE_BODY()";
		public const string FieldNotifyMacroSuffix = "FIELDNOTIFY";
		public const string InClassMacroSuffix = "INCLASS";
		public const string InClassNoPureDeclsMacroSuffix = "INCLASS_NO_PURE_DECLS";
		public const string InClassIInterfaceMacroSuffix = "INCLASS_IINTERFACE";
		public const string InClassIInterfaceNoPureDeclsMacroSuffix = "INCLASS_IINTERFACE_NO_PURE_DECLS";
		public const string PrologMacroSuffix = "PROLOG";
		public const string DelegateMacroSuffix = "DELEGATE";
		public const string AutoGettersSettersMacroSuffix = "AUTOGETTERSETTER_DECLS";

		#endregion


		
		public string FileId => HeaderInfos[HeaderFile.HeaderFileTypeIndex].FileId;
		
		protected List<FusionRPCFunctionData>? parsedFunctions;

		public FusionCodeGenerator(FusionUhtCodeGen codeGenerator, UhtHeaderFile headerFile, List<FusionRPCFunctionData>? parsedFunctions)
			: base(codeGenerator, headerFile)
		{
			this.parsedFunctions = parsedFunctions;
		}
		
		protected static string MakeExportPath(UhtHeaderFile headerFile, string suffix)
		{
#if UE_5_5_OR_LATER
			UhtModule module = headerFile.Module;
			string outputDirectory = module.Module.OutputDirectory;
#else
			UhtPackage package = headerFile.Package;
			string outputDirectory = package.Module.OutputDirectory;
#endif
			
			string fileName = headerFile.FileNameWithoutExtension;
			return Path.Combine(outputDirectory, fileName) + suffix;
		}
	}
}