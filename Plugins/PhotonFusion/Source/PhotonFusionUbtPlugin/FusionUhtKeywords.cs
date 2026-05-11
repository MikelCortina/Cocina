// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Parsers;
using FusionUbtPlugin.UHT.Types;
using FusionUbtPlugin.UHT.Utils;



namespace FusionUbtPlugin
{
	public struct FusionBodyData
	{
		public int MacroLineNumber = -1;

		public FusionBodyData(int LineNumber)
		{
			MacroLineNumber = LineNumber;
		}
	}
	
	public class FusionRPCFunctionData
	{
		public FusionRPCFunction Function { get; private set; }
		
		public FusionRPCFunction ReceiveFunction { get; private set; }

		
		public FusionRPCFunctionData(FusionRPCFunction function, FusionRPCFunction receiveFunction)
		{
			Function = function;
			ReceiveFunction = receiveFunction;
		}
	}
	
    [UnrealHeaderTool]
    internal static class FusionUhtKeywords
    {
	    public static Dictionary<string, List<FusionRPCFunctionData>> parsedFunctions = new Dictionary<string, List<FusionRPCFunctionData>>();
	    public static Dictionary<string, FusionBodyData> bodyData = new Dictionary<string, FusionBodyData>();
	    private static readonly object collectionsLock = new();

	    [UhtKeyword(Extends = UhtTableNames.Class)]
	    [UhtKeyword(Extends = UhtTableNames.Interface)]
	    [UhtKeyword(Extends = UhtTableNames.NativeInterface)]
	    [SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
	    [SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
	    private static UhtParseResult SEND_FUSIONRPCKeyword(UhtParsingScope parentScope, UhtParsingScope actionScope, ref UhtToken token)
	    {
		    UhtSpecifierTable table = parentScope.Session.GetSpecifierTable(UhtTableNames.Function);
		    return ParseFunction(parentScope, actionScope, table, ref token);
	    }

	    [UhtKeywordCatchAll(Extends = UhtTableNames.Global)]
        [SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
        private static UhtParseResult ParseCatchAllKeyword(UhtParsingScope topScope, ref UhtToken token)
        {
	        return UhtParseResult.Unhandled;
        }

        
        [UhtKeyword(Extends = UhtTableNames.Class)]
        [UhtKeyword(Extends = UhtTableNames.ScriptStruct)]
        [SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
        [SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
        public static UhtParseResult FUSION_BODYKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
        {
	        if (actionScope.ScopeType is UhtClass classObj)
	        {
		        FusionBodyData data = new(topScope.TokenReader.InputLine);
		        lock (collectionsLock)
		        {
			        bodyData[classObj.SourceName] = data;
		        }
	        }
	  
	        return UhtParseResult.Handled;
        }

        public static UhtParseResult FUSION_COLLECTIONKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
        {
	        UhtPropertyParseOptions options = UhtPropertyParseOptions.ParseLayoutMacro | UhtPropertyParseOptions.List | UhtPropertyParseOptions.AddModuleRelativePath;
	        
	        #if UE_5_6_OR_LATER
	        UhtPropertyParser.Parse(topScope, EPropertyFlags.ParmFlags, options, UhtPropertyCategory.Member, s_propertyDelegate);
	        #else
	        topScope.HeaderParser.GetCachedPropertyParser().Parse(topScope, EPropertyFlags.ParmFlags, options, UhtPropertyCategory.Member, s_propertyDelegate);
	        #endif
	        topScope.TokenReader.Require(';');

	        // C++ UHT TODO - Skip any extra ';'.  This can be removed if we remove UhtHeaderfileParser.ParserStatement generating errors
	        // when extra ';' are found.  Oddly, UPROPERTY specifically skips extra ';'
	        while (true)
	        {
		        UhtToken nextToken = topScope.TokenReader.PeekToken();
		        if (!nextToken.IsSymbol(';'))
		        {
			        break;
		        }
		        topScope.TokenReader.ConsumeToken();
	        }
	        return UhtParseResult.Handled;
        }
        
        private static readonly UhtPropertyDelegate s_propertyDelegate = PropertyParsed;
        
        
	    private static UhtParseResult ParseFunction(UhtParsingScope parentScope, UhtParsingScope actionScope, UhtSpecifierTable table, ref UhtToken token)
	    {
		    #if UE_5_7_OR_LATER
		    FusionRPCFunction function = new(parentScope.HeaderFile, parentScope.HeaderParser.GetNamespace(), parentScope.ScopeType, token.InputLine);
		    #elif UE_5_5_OR_LATER
			FusionRPCFunction function = new(parentScope.HeaderFile, parentScope.ScopeType, token.InputLine);
			#else
		    FusionRPCFunction function = new(parentScope.HeaderParser.HeaderFile, parentScope.ScopeType, token.InputLine);
		    #endif
		    {
			    using UhtParsingScope topScope = new(parentScope, function, parentScope.Session.GetKeywordTable(UhtTableNames.Function), UhtAccessSpecifier.Public);
			    UhtParsingScope outerClassScope = topScope.CurrentClassScope;
			    UhtClass outerClass = (UhtClass)outerClassScope.ScopeType;
	
			    string scopeName = "function";
			    {
				    using UhtMessageContext tokenContext = new(scopeName);
				    topScope.AddModuleRelativePathToMetaData();

				    UhtSpecifierContext specifierContext = new(topScope, topScope.TokenReader, function.MetaData);
				    UhtSpecifierParser specifierParser = UhtSpecifierParser.GetThreadInstance(specifierContext, scopeName, table);
				    specifierParser.ParseSpecifiers();

				    if (!outerClass.ClassFlags.HasAnyFlags(EClassFlags.Native))
				    {
					    throw new UhtException(function, "Should only be here for native classes!");
				    }

				    function.MacroLineNumber = topScope.TokenReader.InputLine;
				    function.FunctionFlags |= EFunctionFlags.Native;

				    bool automaticallyFinal = true;
				    switch (outerClassScope.AccessSpecifier)
				    {
					    case UhtAccessSpecifier.Public:
						    function.FunctionFlags |= EFunctionFlags.Public;
						    break;

					    case UhtAccessSpecifier.Protected:
						    function.FunctionFlags |= EFunctionFlags.Protected;
						    break;

					    case UhtAccessSpecifier.Private:
						    function.FunctionFlags |= EFunctionFlags.Private | EFunctionFlags.Final;

						    // This is automatically final as well, but in a different way and for a different reason
						    automaticallyFinal = false;
						    break;
				    }

					topScope.TokenReader.OptionalAttributes(false);

					if (topScope.TokenReader.TryOptional("static"))
					{
						function.FunctionFlags |= EFunctionFlags.Static;
						function.FunctionExportFlags |= UhtFunctionExportFlags.CppStatic;
					}

					if (function.MetaData.ContainsKey(UhtNames.CppFromBpEvent))
					{
						function.FunctionFlags |= EFunctionFlags.Event;
					}

					if ((topScope.HeaderParser.GetCurrentCompositeCompilerDirective() & UhtCompilerDirective.WithEditor) != 0)
					{
						function.FunctionFlags |= EFunctionFlags.EditorOnly;
						function.DefineScope |= UhtDefineScope.EditorOnlyData;
					}

					specifierParser.ParseDeferred();
					
					if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
					{
						// Network replicated functions are always events
						function.FunctionFlags |= EFunctionFlags.Event;
					}

					if (function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CustomThunk) && !function.MetaData.ContainsKey(UhtNames.CustomThunk))
					{
						function.MetaData.Add(UhtNames.CustomThunk, true);
					}

					if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
					{
						// Network replicated functions are always events, and are only final if sealed
						scopeName = "event";
						tokenContext.Reset(scopeName);
						automaticallyFinal = false;
					}

					if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
					{
						scopeName = function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native) ? "BlueprintNativeEvent" : "BlueprintImplementableEvent";
						tokenContext.Reset(scopeName);
						automaticallyFinal = false;
					}

					// Record the tokens so we can detect this function as a declaration later (i.e. RPC)
					{
						using UhtTokenRecorder tokenRecorder = new(parentScope, function);

						if (topScope.TokenReader.TryOptional("virtual"))
						{
							function.FunctionExportFlags |= UhtFunctionExportFlags.Virtual;
						}

						bool internalOnly = function.MetaData.GetBoolean(UhtNames.BlueprintInternalUseOnly);

						// Peek ahead to look for a CORE_API style DLL import/export token if present
						if (topScope.TokenReader.TryOptionalAPIMacro(out UhtToken apiMacroToken))
						{
							//@TODO: Validate the module name for RequiredAPIMacroIfPresent
							function.FunctionFlags |= EFunctionFlags.RequiredAPI;
							function.FunctionExportFlags |= UhtFunctionExportFlags.RequiredAPI;
						}

						// Look for static again, in case there was an ENGINE_API token first
						if (apiMacroToken && topScope.TokenReader.TryOptional("static"))
						{
							topScope.TokenReader.LogError($"Unexpected API macro '{apiMacroToken.Value}'. Did you mean to put '{apiMacroToken.Value}' after the static keyword?");
						}

						// Look for virtual again, in case there was an ENGINE_API token first
						if (topScope.TokenReader.TryOptional("virtual"))
						{
							function.FunctionExportFlags |= UhtFunctionExportFlags.Virtual;
						}

						// If virtual, remove the implicit final, the user can still specifying an explicit final at the end of the declaration
						if (function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.Virtual))
						{
							automaticallyFinal = false;
						}
						
						// Handle the initial implicit/explicit final
						// A user can still specify an explicit final after the parameter list as well.
						if (automaticallyFinal || function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.SealedEvent))
						{
							function.FunctionFlags |= EFunctionFlags.Final;
							function.FunctionExportFlags |= UhtFunctionExportFlags.Final | UhtFunctionExportFlags.AutoFinal;
						}

						// Get return type.  C++ style functions always have a return value type, even if it's void
						UhtToken funcNameToken = new();
						UhtProperty? returnValueProperty = null;

						FusionPropertyParser propertyParser = new();
						UhtPropertyParseOptions parserOptions = function.ExternalGetPropertyParseOptions(true);
						
						propertyParser.Parse(topScope, EPropertyFlags.None, parserOptions, UhtPropertyCategory.Return,
							(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType) =>
							{
								property.PropertyFlags |= EPropertyFlags.Parm | EPropertyFlags.OutParm | EPropertyFlags.ReturnParm;
								funcNameToken = nameToken;
								if (property is not UhtVoidProperty)
								{
									throw new UhtException(topScope.TokenReader, "Fusion RPC function cannot have return value/s");
								}
							});

						if (funcNameToken.Value.Length == 0)
						{
							throw new UhtException(topScope.TokenReader, "expected return value and function name");
						}

						// Get function or operator name.
						function.SourceName = funcNameToken.Value.ToString();

						scopeName = $"{scopeName} '{function.SourceName}'";
						tokenContext.Reset(scopeName);
						
						topScope.TokenReader.Require('(');
						
						//Set the various implementation specific c++ names.
						UhtFunctionHelpers.SetFunctionNames(function);
						
						//Add the parameters from parsed string data
						UhtFunctionHelpers.ParseParameterList(topScope, propertyParser);
		
						//Add to parent (assumed to be a UhtClass) //TODO: Check for this later...
						UhtFunctionHelpers.AddFunctionToParentScope(function);
						
						// Add back in the return value
						if (returnValueProperty != null)
						{
							topScope.ScopeType.AddChild(returnValueProperty);
						}

						// determine whether this function should be 'const'
						if (topScope.TokenReader.TryOptional("const"))
						{
							if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
							{
								// @TODO: UCREMOVAL Reconsider?
								//Throwf(TEXT("'const' may only be used for native functions"));
							}

							function.FunctionFlags |= EFunctionFlags.Const;
							function.FunctionExportFlags |= UhtFunctionExportFlags.DeclaredConst;
						}
						
						// Try parsing metadata for the function
						specifierParser.ParseFieldMetaData();

						// COMPATIBILITY-TODO - Try to pull any comment following the declaration
						topScope.TokenReader.PeekToken();
						topScope.TokenReader.CommitPendingComments();

						topScope.AddFormattedCommentsAsTooltipMetaData();

						// 'final' and 'override' can appear in any order before an optional '= 0' pure virtual specifier
						bool foundFinal = topScope.TokenReader.TryOptional("final");
						bool foundOverride = topScope.TokenReader.TryOptional("override");
						if (!foundFinal && foundOverride)
						{
							foundFinal = topScope.TokenReader.TryOptional("final");
						}

						// Handle C++ style functions being declared as abstract
						if (topScope.TokenReader.TryOptional('='))
						{
							bool gotZero = topScope.TokenReader.TryOptionalConstInt(out int zeroValue);
							gotZero = gotZero && (zeroValue == 0);
							if (!gotZero || zeroValue != 0)
							{
								throw new UhtException(topScope.TokenReader, "Expected 0 to indicate function is abstract");
							}
						}

						// Look for the final keyword to indicate this function is sealed
						if (foundFinal)
						{
							// This is a final (prebinding, non-overridable) function
							function.FunctionFlags |= EFunctionFlags.Final;
							function.FunctionExportFlags |= UhtFunctionExportFlags.Final;
						}

						// Optionally consume a semicolon
						// This is optional to allow inline function definitions
						if (topScope.TokenReader.TryOptional(';'))
						{
							// Do nothing (consume it)
						}
						else if (topScope.TokenReader.TryPeekOptional('{'))
						{
							// Skip inline function bodies
							#if UE_5_7_OR_LATER
							topScope.SkipDeclaration(new());
							#else
							UhtToken tokenCopy = new();
							topScope.TokenReader.SkipDeclaration(ref tokenCopy);
							#endif
						}
					}
			    }
		    }
		    
		    string receiveFunctionName = $"{function.CppImplName}_Receive";

		    FusionRPCFunction receiveFunction = AddReceiveFunction(parentScope, actionScope, function, token.InputLine, receiveFunctionName);
		    UhtFunctionHelpers.AddFunctionToParentScope(receiveFunction);
		    
		    //Add hidden property to class to mark the class as having fusion RPCs
		    AddHiddenProperty(parentScope, actionScope, receiveFunctionName);
						
		    if (function.Outer != null)
		    {
			    FusionRPCFunctionData functionData = new(function, receiveFunction);
			    lock (collectionsLock)
			    {
				    if (parsedFunctions.TryGetValue(function.Outer.SourceName, out List<FusionRPCFunctionData>? functions))
				    {
					    functions.Add(functionData);
				    }
				    else
				    {
					    parsedFunctions.Add(function.Outer.SourceName, new List<FusionRPCFunctionData> { functionData });
				    }
			    }
		    }

		    return UhtParseResult.Handled;
	    }
	    
	    private static FusionRPCFunction AddReceiveFunction(UhtParsingScope parentScope, UhtParsingScope actionScope, UhtFunction function, int startLine, string receiveFunctionName)
	    {
		    #if UE_5_7_OR_LATER
		    FusionRPCFunction receiveFunction = new(parentScope.HeaderFile, parentScope.HeaderParser.GetNamespace(), parentScope.ScopeType, startLine);
			#elif UE_5_5_OR_LATER
			FusionRPCFunction receiveFunction = new(parentScope.HeaderFile, parentScope.ScopeType, startLine);
			#else
		    FusionRPCFunction receiveFunction = new(parentScope.HeaderParser.HeaderFile, parentScope.ScopeType, startLine);
			#endif

		    receiveFunction.MacroLineNumber = function.LineNumber;
		    receiveFunction.FunctionFlags = function.FunctionFlags;
		    receiveFunction.FunctionExportFlags = function.FunctionExportFlags;
		    receiveFunction.DefineScope = function.DefineScope;
		    receiveFunction.MetaData = function.MetaData;
		    receiveFunction.SourceName = receiveFunctionName; //If no defined c++ function is found this will throw a compiler error, this is good. Forces developer to define one.
		    
		    UhtFunctionHelpers.SetFunctionNames(receiveFunction);
		    
		    //Fully copy of all parameters, avoid cross referencing between functions. (Causes issue when compiling)
		    foreach (UhtProperty parameter in function.Children)
		    {
			    UhtPropertySettings propertySettings = new();
			    propertySettings.Reset(parameter, 0, UhtPropertyCategory.Member, EPropertyFlags.ParmFlags);
			    
#if UE_5_6_OR_LATER
			    propertySettings.TypeTokens = parameter.TypeTokens;
#endif
				
			    UhtProperty clonedProperty = CloneProperty(parameter, propertySettings);
			    
			    clonedProperty.Outer = receiveFunction;
			    clonedProperty.PropertyFlags = parameter.PropertyFlags;
			    clonedProperty.DisallowPropertyFlags = parameter.DisallowPropertyFlags;
			    clonedProperty.PropertyCaps = parameter.PropertyCaps;
			    clonedProperty.PropertyExportFlags = parameter.PropertyExportFlags;
			    clonedProperty.EngineName = parameter.EngineName;
			    clonedProperty.SourceName = parameter.SourceName;
			    clonedProperty.PropertyCategory = parameter.PropertyCategory;
				    
			    receiveFunction.AddChild(clonedProperty);
		    }

		    return receiveFunction;
	    }

	    private static UhtProperty CloneProperty(UhtProperty parameter, UhtPropertySettings settings)
	    {
		    // Types that require extra constructor arguments
		    switch (parameter)
		    {
			    case UhtBoolProperty p:
				    return new UhtBoolProperty(settings, p.BoolType);
			    case UhtEnumProperty p:
				    return new UhtEnumProperty(settings, p.Enum);
			    case UhtByteProperty p:
#if UE_5_7_OR_LATER
				    return new UhtByteProperty(settings, enumObj: p.Enum);
#else
				    return new UhtByteProperty(settings, p.Enum);
#endif
			    case FusionPreResolveProperty p:
#if UE_5_6_OR_LATER
				    return new FusionPreResolveProperty(settings, p.PropertySettings.TypeTokens.AllTokens);
#else
				    return new FusionPreResolveProperty(settings, p.TypeTokens);
#endif
			    case UhtPreResolveProperty p:
#if UE_5_6_OR_LATER
				    return new UhtPreResolveProperty(settings);
#else
				    return new UhtPreResolveProperty(settings, p.TypeTokens);
#endif
		    }

		    // Simple types: clone via constructor(UhtPropertySettings)
		    ConstructorInfo? ctor = parameter.GetType().GetConstructor(new[] { typeof(UhtPropertySettings) });
		    if (ctor != null)
		    {
			    return (UhtProperty)ctor.Invoke(new object[] { settings });
		    }

		    throw new Exception($"Unable to clone property of type {parameter.GetType().Name}: {parameter}");
	    }

	    private static void AddHiddenProperty(UhtParsingScope parentScope, UhtParsingScope actionScope, string receiveFunctionName)
	    {
		    using UhtThreadBorrower<UhtPropertySpecifierContext> borrower = new(true);
		    UhtPropertySpecifierContext specifierContext = borrower.Instance;
		    specifierContext.Type = parentScope.ScopeType;
		    specifierContext.TokenReader = parentScope.TokenReader;
		    specifierContext.AccessSpecifier = parentScope.AccessSpecifier;
		    specifierContext.MessageSite = parentScope.TokenReader;
		    specifierContext.PropertySettings.Reset(specifierContext.Type, 0, UhtPropertyCategory.Member, EPropertyFlags.ParmFlags);
		    specifierContext.MetaData = specifierContext.PropertySettings.MetaData;
		    specifierContext.MetaNameIndex = UhtMetaData.IndexNone;
		    specifierContext.SeenEditSpecifier = false;
		    specifierContext.SeenBlueprintWriteSpecifier = false;
		    specifierContext.SeenBlueprintReadOnlySpecifier = false;
		    specifierContext.SeenBlueprintGetterSpecifier = false;
		    
		    UhtPropertySettings propertySettings = specifierContext.PropertySettings;
		    UhtSession session = propertySettings.Outer.Session;

		    if (parentScope.ScopeType is UhtClass classScope)
		    {
			    UhtBoolProperty newProperty = new(propertySettings, UhtBoolType.Native);
			    newProperty.SourceName = $"__FUSIONRPCEVENT_{receiveFunctionName}";

			    if (classScope.Properties.FirstOrDefault(p => p.SourceName == newProperty.SourceName) is null)
			    {
				    classScope.AddChild(newProperty);
			    }
		    }
	    }

        private static void PropertyParsed(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType)
        {
	        IUhtTokenReader tokenReader = topScope.TokenReader;

	        // Skip any initialization
	        if (tokenReader.TryOptional('='))
	        {
		        tokenReader.SkipUntil(';');
	        }
	        else if (tokenReader.TryOptional('{'))
	        {
		        tokenReader.SkipBrackets('{', '}', 1);
	        }
        }
    }
}