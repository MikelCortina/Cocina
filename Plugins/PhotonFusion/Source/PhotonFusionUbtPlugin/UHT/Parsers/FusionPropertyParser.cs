// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Threading;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace FusionUbtPlugin.UHT.Parsers
{
	internal record struct UhtPropertyCategoryExtraContext(UhtPropertyCategory Category) : IUhtMessageExtraContext
	{
		public IEnumerable<object?>? MessageExtraContext
		{
			get
			{
				Stack<object?> extraContext = new(1);
				extraContext.Push(Category.GetHintText());
				return extraContext;
			}
		}
	}
	
	public class FusionPropertyParser
	{
		private static readonly Dictionary<StringView, UhtLayoutMacroType> s_layoutMacroTypes = new(new[]
		{
			UhtLayoutMacroType.Array.MacroNameAndValue(),
			UhtLayoutMacroType.ArrayEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Bitfield.MacroNameAndValue(),
			UhtLayoutMacroType.BitfieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.Field.MacroNameAndValue(),
			UhtLayoutMacroType.FieldEditorOnly.MacroNameAndValue(),
			UhtLayoutMacroType.FieldInitialized.MacroNameAndValue(),
		});
		
#if !UE_5_6_OR_LATER
		private readonly UhtTokensUntilDelegate _gatherTypeTokensDelegate;
		
		// Scratch pad variables used by actions
		private IUhtTokenReader? _currentTokenReader = null;
		private List<UhtToken> _currentTypeTokens = new();
		private int _currentTemplateDepth = 0;
#endif
		
		private static readonly ThreadLocal<UhtPropertySettings> s_tlsPropertySettings = new(() => { return new UhtPropertySettings(); });
		
		public FusionPropertyParser()
		{
#if !UE_5_6_OR_LATER
			_gatherTypeTokensDelegate = GatherTypeTokens;
#endif
		}
		
		public void Parse(UhtParsingScope topScope, EPropertyFlags disallowPropertyFlags, UhtPropertyParseOptions options, UhtPropertyCategory category, UhtPropertyDelegate propertyDelegate)
		{
			// Initialize the property context
			using UhtThreadBorrower<UhtPropertySpecifierContext> borrower = new(true);
			UhtPropertySpecifierContext specifierContext = borrower.Instance;
			specifierContext.Type = topScope.ScopeType;
			specifierContext.TokenReader = topScope.TokenReader;
			specifierContext.AccessSpecifier = topScope.AccessSpecifier;
			specifierContext.MessageSite = topScope.TokenReader;
			specifierContext.PropertySettings.Reset(specifierContext.Type, 0, category, disallowPropertyFlags);
			specifierContext.MetaData = specifierContext.PropertySettings.MetaData;
			specifierContext.MetaNameIndex = UhtMetaData.IndexNone;
			specifierContext.SeenEditSpecifier = false;
			specifierContext.SeenBlueprintWriteSpecifier = false;
			specifierContext.SeenBlueprintReadOnlySpecifier = false;
			specifierContext.SeenBlueprintGetterSpecifier = false;

#if UE_5_6_OR_LATER
			UhtPropertyCategoryExtraContext extraContext = new(category);
			using UhtMessageContext tokenContext = new(extraContext);
#else
			// Initialize the settings
			_currentTokenReader = topScope.TokenReader;
			_currentTypeTokens = new List<UhtToken>();
			_currentTemplateDepth = 0;
			using UhtMessageContext tokenContext = new(this);
#endif
			ParseInternal(topScope, options, specifierContext, propertyDelegate);
		}
		
#pragma warning disable CA1822
		private void ParseInternal(UhtParsingScope topScope, UhtPropertyParseOptions options, UhtPropertySpecifierContext specifierContext, UhtPropertyDelegate propertyDelegate)
#pragma warning restore CA1822
		{
			UhtPropertySettings propertySettings = specifierContext.PropertySettings;
			IUhtTokenReader tokenReader = topScope.TokenReader;

			propertySettings.LineNumber = tokenReader.InputLine;

			if (propertySettings.PropertyCategory == UhtPropertyCategory.Member)
			{
				UhtCompilerDirective compilerDirective = topScope.HeaderParser.GetCurrentCompositeCompilerDirective();
				if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditorOnlyData))
				{
					propertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
					propertySettings.DefineScope |= UhtDefineScope.EditorOnlyData;
				}
				else if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithEditor))
				{
					// Checking for this error is a bit tricky given legacy code.  
					// 1) If already wrapped in WITH_EDITORONLY_DATA (see above), then we ignore the error via the else 
					// 2) Ignore any module that is an editor module
					
					#if UE_5_5_OR_LATER
					UHTManifest.Module module = topScope.Module.Module;
					#else
					UhtPackage package = topScope.ScopeType.HeaderFile.Package;
					UHTManifest.Module module = package.Module;
					#endif
					
					bool isEditorModule =
						module.ModuleType == UHTModuleType.EngineEditor ||
						module.ModuleType == UHTModuleType.GameEditor ||
						module.ModuleType == UHTModuleType.EngineUncooked ||
						module.ModuleType == UHTModuleType.GameUncooked;
					if (!isEditorModule)
					{
						tokenReader.LogError("UProperties should not be wrapped by WITH_EDITOR, use WITH_EDITORONLY_DATA instead.");
					}
				}
				if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithVerseVM))
				{
					propertySettings.DefineScope |= UhtDefineScope.VerseVM;
				}
#if UE_5_6_OR_LATER
				if (compilerDirective.HasAnyFlags(UhtCompilerDirective.WithTests))
				{
					propertySettings.DefineScope |= UhtDefineScope.Tests;
				}
#endif
			}

			// Parse type information including UPARAM that might appear in template arguments
#if UE_5_6_OR_LATER
			UhtPropertyParser.PreParseType(specifierContext, false);
#else
			PreParseTypeInternal(specifierContext, false);
#endif

			// Swallow inline keywords
			if (propertySettings.PropertyCategory == UhtPropertyCategory.Return)
			{
				tokenReader
					.Optional("inline")
					.Optional("FORCENOINLINE")
					.OptionalStartsWith("FORCEINLINE");
			}

			// Handle MemoryLayout.h macros
			bool hasWrapperBrackets = false;
			UhtLayoutMacroType layoutMacroType = UhtLayoutMacroType.None;
			if (options.HasAnyFlags(UhtPropertyParseOptions.ParseLayoutMacro))
			{
				ref UhtToken layoutToken = ref tokenReader.PeekToken();
				if (layoutToken.IsIdentifier())
				{
					if (s_layoutMacroTypes.TryGetValue(layoutToken.Value, out layoutMacroType))
					{
						tokenReader.ConsumeToken();
						tokenReader.Require('(');
						hasWrapperBrackets = tokenReader.TryOptional('(');
						if (layoutMacroType.IsEditorOnly())
						{
							propertySettings.PropertyFlags |= EPropertyFlags.EditorOnly;
							propertySettings.DefineScope |= UhtDefineScope.EditorOnlyData;
						}
					}
				}

				// This exists as a compatibility "shim" with UHT4/5.0.  If the fetched token wasn't an identifier,
				// it wasn't returned to the tokenizer.  So, just consume the token here.  In theory, this should be
				// removed once we have a good deprecated system.
				//@TODO - deprecate
				else // if (LayoutToken.IsSymbol(';'))
				{
					tokenReader.ConsumeToken();
				}
			}

			//@TODO: Should flag as settable from a const context, but this is at least good enough to allow use for C++ land
			tokenReader.Optional("mutable");
			
			#if UE_5_6_OR_LATER
			// Gather the type tokens and possibly the property name.
			UhtTypeTokens typeTokens = UhtTypeTokens.Gather(tokenReader);
			
			// Verify we at least have one type
			if (typeTokens.AllTokens.Length < 1)
			{
				throw new UhtException(tokenReader, $"{propertySettings.PropertyCategory.GetHintText()}: Missing variable type or name");
			}
			#else
			// Gather the type tokens and possibly the property name.
			tokenReader.While(_gatherTypeTokensDelegate);
			
			// Verify we at least have one type
			if (_currentTypeTokens.Count < 1)
			{
				throw new UhtException(tokenReader, $"{propertySettings.PropertyCategory.GetHintText()}: Missing variable type or name");
			}
			#endif

			// Consume the wrapper brackets.  This is just an extra set
			if (hasWrapperBrackets)
			{
				tokenReader.Require(')');
			}

			// Check for any disallowed flags
			if (propertySettings.PropertyFlags.HasAnyFlags(propertySettings.DisallowPropertyFlags))
			{
				EPropertyFlags extraFlags = propertySettings.PropertyFlags & propertySettings.DisallowPropertyFlags;
				tokenReader.LogError($"Specified type modifiers not allowed here '{String.Join(" | ", extraFlags.ToStringList())}'");
			}

			if (options.HasAnyFlags(UhtPropertyParseOptions.AddModuleRelativePath))
			{
				#if UE_5_5_OR_LATER
				UhtParsingScope.AddModuleRelativePathToMetaData(propertySettings.MetaData, topScope.HeaderFile);
				#else
				UhtParsingScope.AddModuleRelativePathToMetaData(propertySettings.MetaData, topScope.ScopeType.HeaderFile);
				#endif
			}

			// Fetch the name of the property, bitfield and array size
			if (layoutMacroType != UhtLayoutMacroType.None)
			{
				tokenReader.Require(',');
				UhtToken nameToken = tokenReader.GetIdentifier();
				if (layoutMacroType.IsArray())
				{
					tokenReader.Require(',');
					RequireArray(tokenReader, propertySettings, ref nameToken, ')');
					tokenReader.Require(')');
				}
				else if (layoutMacroType.IsBitfield())
				{
					tokenReader.Require(',');
					RequireBitfield(tokenReader, propertySettings, ref nameToken);
					tokenReader.Require(')');
				}
				else if (layoutMacroType.HasInitializer())
				{
					tokenReader.Require(',');
					tokenReader.SkipBrackets('(', ')', 1); // consumes ending ) too
				}
				else
				{
					tokenReader.Require(')');
				}

#if UE_5_6_OR_LATER
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#else
				Finalize(topScope, options, specifierContext, ref nameToken, new ReadOnlyMemory<UhtToken>(_currentTypeTokens.ToArray()), layoutMacroType, propertyDelegate);
#endif
			}
			else if (options.HasAnyFlags(UhtPropertyParseOptions.List))
			{
#if UE_5_6_OR_LATER
				UhtToken nameToken = typeTokens.ExtractTrailingIdentifier(tokenReader, propertySettings);
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);

				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);

				// Check for unsupported comma delimited properties
				if (tokenReader.TryOptional(','))
				{
					throw new UhtException(tokenReader, $"Comma delimited properties are not supported");
				}
#else
				// Extract the property name from the types
				if (_currentTypeTokens.Count < 2 || !_currentTypeTokens[^1].IsIdentifier())
				{
					throw new UhtException(tokenReader, $"{propertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken nameToken = _currentTypeTokens[^1];
				_currentTypeTokens.RemoveAt(_currentTypeTokens.Count - 1);
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);

				ReadOnlyMemory<UhtToken> typeTokens = new(_currentTypeTokens.ToArray());
				
				while (true)
				{
					UhtProperty _ = Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);

					// If we have reached the end
					if (!tokenReader.TryOptional(','))
					{
						break;
					}

					// While we could continue parsing, the old UHT would flag this as an error.
					throw new UhtException(tokenReader, $"Comma delimited properties cannot be converted");
				}
#endif
			}
			else if (options.HasAnyFlags(UhtPropertyParseOptions.CommaSeparatedName))
			{
				tokenReader.Require(',');
				UhtToken nameToken = tokenReader.GetIdentifier();
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);
				
#if UE_5_6_OR_LATER
				ValidateParameter(specifierContext, typeTokens.AllTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#else
				ReadOnlyMemory<UhtToken> typeTokens = new ReadOnlyMemory<UhtToken>(_currentTypeTokens.ToArray());
				ValidateParameter(specifierContext, typeTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#endif
			}
			else if (options.HasAnyFlags(UhtPropertyParseOptions.FunctionNameIncluded))
			{
#if UE_5_6_OR_LATER
				UhtToken nameToken = typeTokens.AllTokens.Span[^1];
				nameToken.Value = new StringView("Function");
				if (CheckForOptionalParts(tokenReader, propertySettings, ref nameToken))
				{
					nameToken = tokenReader.GetIdentifier("function name");
				}
				else
				{
					nameToken = typeTokens.ExtractTrailingIdentifier(tokenReader, propertySettings);
				}
				
				ValidateParameter(specifierContext, typeTokens.AllTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#else
				UhtToken nameToken = _currentTypeTokens[^1];
				nameToken.Value = new StringView("Function");
				if (CheckForOptionalParts(tokenReader, propertySettings, ref nameToken))
				{
					nameToken = tokenReader.GetIdentifier("function name");
				}
				else
				{
					if (_currentTypeTokens.Count < 2 || !_currentTypeTokens[^1].IsIdentifier())
					{
						throw new UhtException(tokenReader, $"{propertySettings.PropertyCategory.GetHintText()}: Expected name");
					}
					nameToken = _currentTypeTokens[^1];
					_currentTypeTokens.RemoveAt(_currentTypeTokens.Count - 1);
				}

				ReadOnlyMemory<UhtToken> typeTokens = new ReadOnlyMemory<UhtToken>(_currentTypeTokens.ToArray());
				ValidateParameter(specifierContext, typeTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#endif
			}
			else if (options.HasAnyFlags(UhtPropertyParseOptions.NameIncluded))
			{
#if UE_5_6_OR_LATER
				UhtToken nameToken = typeTokens.ExtractTrailingIdentifier(tokenReader, propertySettings);
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);
				
				ValidateParameter(specifierContext, typeTokens.AllTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#else
				if (_currentTypeTokens.Count < 2 || !_currentTypeTokens[^1].IsIdentifier())
				{
					throw new UhtException(tokenReader, $"{propertySettings.PropertyCategory.GetHintText()}: Expected name");
				}
				UhtToken nameToken = _currentTypeTokens[^1];
				_currentTypeTokens.RemoveAt(_currentTypeTokens.Count - 1);
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);
				
				ReadOnlyMemory<UhtToken> typeTokens = new ReadOnlyMemory<UhtToken>(_currentTypeTokens.ToArray());
				ValidateParameter(specifierContext, typeTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#endif
			}
			else
			{
				UhtToken nameToken = new();
				CheckForOptionalParts(tokenReader, propertySettings, ref nameToken);
				
#if UE_5_6_OR_LATER
				ValidateParameter(specifierContext, typeTokens.AllTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#else
				ReadOnlyMemory<UhtToken> typeTokens = new ReadOnlyMemory<UhtToken>(_currentTypeTokens.ToArray());
				ValidateParameter(specifierContext, typeTokens, nameToken);
				Finalize(topScope, options, specifierContext, ref nameToken, typeTokens, layoutMacroType, propertyDelegate);
#endif
			}
		}

		private static void ValidateParameter(UhtPropertySpecifierContext specifierContext, ReadOnlyMemory<UhtToken> typeTokens, UhtToken nameToken)
		{
			// Loop through the tokens until we find a known property type or the start of a template argument list
			for (int index = 0; index < typeTokens.Length; ++index)
			{
				if (typeTokens.Span[index].IsSymbol())
				{
					ReadOnlySpan<char> span = typeTokens.Span[index].Value.Span;
					if (span.Length == 1 && (span[0] == '<' || span[0] == '>' || span[0] == ','))
					{
						break;
					}
				}
				else if (typeTokens.Span[index].IsIdentifier())
				{
					UhtToken copy = typeTokens.Span[index];
					if (copy.Value == "FString")
					{
						if (specifierContext.PropertySettings.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
						{
							throw new Exception($"Fusion RPC function with parameter: {nameToken.Value} expects FString to not have const, make sure strings are passed by just &");
						}
						
						//Ensure there is another token ahead, we only allows FStrings by const ref as parameters
						if (typeTokens.Length > index+1)
						{
							UhtToken next = typeTokens.Span[index+1];

							if (next.Value != "&")
							{
								throw new Exception($"Fusion RPC function with parameter: {nameToken.Value}  expects FString to be a &, make sure strings are passed by &");
							}
						}
						else
						{
							throw new Exception($"Fusion RPC function with parameter: {nameToken.Value}  expects FString to be a &, make sure strings are passed by &");
						}
					}
				}
			}
		}

#if UE_5_6_OR_LATER
		private static UhtProperty Finalize(UhtParsingScope topScope, UhtPropertyParseOptions options,
			UhtPropertySpecifierContext specifierContext, ref UhtToken nameToken,
			UhtTypeTokens typeTokens, UhtLayoutMacroType layoutMacroType, UhtPropertyDelegate propertyDelegate)
#else
		private static UhtProperty Finalize(UhtParsingScope topScope, UhtPropertyParseOptions options, UhtPropertySpecifierContext specifierContext, ref UhtToken nameToken, 
			ReadOnlyMemory<UhtToken> typeTokens, UhtLayoutMacroType layoutMacroType, UhtPropertyDelegate propertyDelegate)
#endif
		{
			UhtPropertySettings propertySettings = specifierContext.PropertySettings;
			
#if UE_5_6_OR_LATER
			propertySettings.TypeTokens = new(typeTokens, 0);
#endif
			IUhtTokenReader tokenReader = specifierContext.TokenReader;

			propertySettings.SourceName = propertySettings.PropertyCategory == UhtPropertyCategory.Return ? "ReturnValue" : nameToken.Value.ToString();
			
#if UE_5_6_OR_LATER
			UhtProperty? attemptedProperty = ResolveProperty(UhtPropertyResolvePhase.Parsing, propertySettings);
#else
			UhtProperty? attemptedProperty = UhtPropertyParser.ResolveProperty(UhtPropertyResolvePhase.Parsing, propertySettings, propertySettings.Outer.HeaderFile.Data.Memory, typeTokens);
#endif
			
			//if concrete type cannot be solved here because of its type dependency we leave it later for the generator phase to figure out.
#if UE_5_6_OR_LATER
			UhtProperty newProperty = attemptedProperty ?? new FusionPreResolveProperty(propertySettings, typeTokens.AllTokens);
#else
			UhtProperty newProperty = attemptedProperty ?? new FusionPreResolveProperty(propertySettings, typeTokens);
#endif
			
			// Force the category in non-engine projects
			if (newProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				#if UE_5_5_OR_LATER
				bool isPartOfEngine = newProperty.Module.IsPartOfEngine;
				#else
				bool isPartOfEngine = newProperty.Package.IsPartOfEngine;
				#endif
				
				if (!isPartOfEngine &&
					newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible) &&
					!newProperty.MetaData.ContainsKey(UhtNames.Category))
				{
					newProperty.MetaData.Add(UhtNames.Category, newProperty.Outer!.EngineName);
				}
			}

			// Check to see if the variable is deprecated, and if so set the flag
			{
				int deprecatedIndex = newProperty.SourceName.IndexOf("_DEPRECATED", StringComparison.Ordinal);
				int nativizedPropertyPostfixIndex = newProperty.SourceName.IndexOf("__pf", StringComparison.Ordinal); //@TODO: check OverrideNativeName in Meta Data, to be sure it's not a random occurrence of the "__pf" string.
				bool ignoreDeprecatedWord = (nativizedPropertyPostfixIndex != -1) && (nativizedPropertyPostfixIndex > deprecatedIndex);
				if ((deprecatedIndex != -1) && !ignoreDeprecatedWord)
				{
					if (deprecatedIndex != newProperty.SourceName.Length - 11)
					{
						tokenReader.LogError("Deprecated variables must end with _DEPRECATED");
					}

					// We allow deprecated properties in blueprints that have getters and setters assigned as they may be part of a backwards compatibility path
					bool blueprintVisible = newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible);
					bool warnOnGetter = blueprintVisible && !newProperty.MetaData.ContainsKey(UhtNames.BlueprintGetter);
					bool warnOnSetter = blueprintVisible && !newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) && !newProperty.MetaData.ContainsKey(UhtNames.BlueprintSetter);

					if (warnOnGetter)
					{
						tokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as blueprint visible without having a BlueprintGetter");
					}

					if (warnOnSetter)
					{
						tokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as blueprint writable without having a BlueprintSetter");
					}

					// Warn if a deprecated property is visible
					if (newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.EditConst) || // Property is marked as editable
						(!blueprintVisible && newProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintReadOnly) &&
						!newProperty.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.ImpliedBlueprintPure))) // Is BPRO, but not via Implied Flags and not caught by Getter/Setter path above
					{
						tokenReader.LogWarning($"{newProperty.PropertyCategory.GetHintText()}: Deprecated property '{newProperty.SourceName}' should not be marked as visible or editable");
					}

					newProperty.PropertyFlags |= EPropertyFlags.Deprecated;
					newProperty.EngineName = newProperty.SourceName[..deprecatedIndex];
				}
			}

			// Try gathering metadata for member fields
			if (newProperty.PropertyCategory == UhtPropertyCategory.Member)
			{
				UhtSpecifierParser specifiers = UhtSpecifierParser.GetThreadInstance(specifierContext, newProperty.SourceName, 
					specifierContext.Type.Session.GetSpecifierTable(UhtTableNames.PropertyMember));
				specifiers.ParseFieldMetaData();
				tokenReader.SkipWhitespaceAndComments(); //TODO - old UHT compatibility.  Commented out initializers can cause comment/tooltip to be used as meta data.
				tokenReader.CommitPendingComments(); //TODO - old UHT compatibility.  Commented out initializers can cause comment/tooltip to be used as meta data.
				topScope.AddFormattedCommentsAsTooltipMetaData(newProperty);
			}

			
#if UE_5_5_OR_LATER
			// Adjust the name for verse
			(bool wasMangled, string result) = newProperty.GetMangledEngineName();
			if (wasMangled)
			{
				if (!newProperty.MetaData.ContainsKey(UhtNames.DisplayName))
				{
					newProperty.MetaData.Add(UhtNames.DisplayName, newProperty.StrippedEngineName);
				}
				newProperty.EngineName = result;
			}
			
#endif

			propertyDelegate(topScope, newProperty, ref nameToken, layoutMacroType);

			// Void properties don't get added when they are the return value
			if (newProperty.PropertyCategory != UhtPropertyCategory.Return || !options.HasAnyFlags(UhtPropertyParseOptions.DontAddReturn))
			{
				topScope.ScopeType.AddChild(newProperty);
			}
			return newProperty;
		}

		
#if !UE_5_6_OR_LATER
		private static void PreParseTypeInternal(UhtPropertySpecifierContext specifierContext, bool isTemplateArgument)
		{
			UhtPropertySettings propertySettings = specifierContext.PropertySettings;
			IUhtTokenReader tokenReader = specifierContext.TokenReader;
			UhtSession session = specifierContext.Type.Session;

			// We parse specifiers when:
			//
			// 1. This is the start of a member property (but not a template)
			// 2. The UPARAM identifier is found
			bool isMember = propertySettings.PropertyCategory == UhtPropertyCategory.Member;
			bool parseSpecifiers = (isMember && !isTemplateArgument) || tokenReader.TryOptional("UPARAM");

			UhtSpecifierParser specifiers = UhtSpecifierParser.GetThreadInstance(specifierContext, "Variable",
				isMember ? session.GetSpecifierTable(UhtTableNames.PropertyMember) : session.GetSpecifierTable(UhtTableNames.PropertyArgument));
			if (parseSpecifiers)
			{
				specifiers.ParseSpecifiers();
			}

			if (propertySettings.PropertyCategory != UhtPropertyCategory.Member && !isTemplateArgument)
			{
				// const before the variable type support (only for params)
				if (tokenReader.TryOptional("const"))
				{
					propertySettings.PropertyFlags |= EPropertyFlags.ConstParm;
					propertySettings.MetaData.Add(UhtNames.NativeConst, "");
				}
			}

			// Process the specifiers
			if (parseSpecifiers)
			{
				specifiers.ParseDeferred();
			}

			// If we saw a BlueprintGetter but did not see BlueprintSetter or 
			// or BlueprintReadWrite then treat as BlueprintReadOnly
			if (specifierContext.SeenBlueprintGetterSpecifier && !specifierContext.SeenBlueprintWriteSpecifier)
			{
				propertySettings.PropertyFlags |= EPropertyFlags.BlueprintReadOnly;
			}

			if (propertySettings.MetaData.ContainsKey(UhtNames.ExposeOnSpawn))
			{
				propertySettings.PropertyFlags |= EPropertyFlags.ExposeOnSpawn;
			}

			if (!isTemplateArgument)
			{
				UhtAccessSpecifier accessSpecifier = specifierContext.AccessSpecifier;
				if (accessSpecifier == UhtAccessSpecifier.Public || propertySettings.PropertyCategory != UhtPropertyCategory.Member)
				{
					propertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
					propertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Public;
					propertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Private | UhtPropertyExportFlags.Protected);

					propertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
					propertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPublic;
				}
				else if (accessSpecifier == UhtAccessSpecifier.Protected)
				{
					propertySettings.PropertyFlags |= EPropertyFlags.Protected;
					propertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Protected;
					propertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Private);

					propertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
					propertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierProtected;
				}
				else if (accessSpecifier == UhtAccessSpecifier.Private)
				{
					propertySettings.PropertyFlags &= ~EPropertyFlags.Protected;
					propertySettings.PropertyExportFlags |= UhtPropertyExportFlags.Private;
					propertySettings.PropertyExportFlags &= ~(UhtPropertyExportFlags.Public | UhtPropertyExportFlags.Protected);

					propertySettings.PropertyFlags &= ~EPropertyFlags.NativeAccessSpecifiers;
					propertySettings.PropertyFlags |= EPropertyFlags.NativeAccessSpecifierPrivate;
				}
				else
				{
					throw new UhtIceException("Unknown access level");
				}
			}
		}
#endif
		
		public static void ResolveChildren(UhtType type, UhtPropertyParseOptions options)
		{
			UhtPropertyOptions propertyOptions = UhtPropertyOptions.None;
			if (options.HasAnyFlags(UhtPropertyParseOptions.NoAutoConst))
			{
				propertyOptions |= UhtPropertyOptions.NoAutoConst;
			}
			bool inSymbolTable = type.EngineType.AddChildrenToSymbolTable();

			UhtPropertySettings? propertySettings = s_tlsPropertySettings.Value;
			if (propertySettings == null)
			{
				throw new UhtIceException("Unable to acquire threaded property settings");
			}

			for (int index = 0; index < type.Children.Count; ++index)
			{
				
				// This is the only change from UhtPropertyParser.ResolveChildren
				// Instead of checking if it is UhtPreResolveProperty we check if its a FusionPreResolveProperty
				if (type.Children[index] is FusionPreResolveProperty property)
				{
					propertySettings.Reset(property, propertyOptions);
					
					#if UE_5_6_OR_LATER
					UhtProperty? resolved = ResolveProperty(UhtPropertyResolvePhase.Resolving, propertySettings);
					#else
					UhtProperty? resolved = UhtPropertyParser.ResolveProperty(UhtPropertyResolvePhase.Resolving, propertySettings, property.HeaderFile.Data.Memory, property.TypeTokens);
					#endif
					
					if (resolved != null)
					{
						if (inSymbolTable && resolved != property)
						{
							type.Session.ReplaceTypeInSymbolTable(property, resolved);
						}
						type.Children[index] = resolved;
					}
					else
					{
						throw new UhtException("Unable to resolve property");
					}
				}
			}
		}
		
#if !UE_5_6_OR_LATER
		private bool GatherTypeTokens(ref UhtToken token)
		{
			if (_currentTemplateDepth == 0 && token.IsSymbol() && (token.IsValue(',') || token.IsValue('(') || token.IsValue(')') || 
			                                                       token.IsValue(';') || token.IsValue('[') || token.IsValue(':') || token.IsValue('=') || token.IsValue('{')))
			{
				return false;
			}

			_currentTypeTokens.Add(token);
			if (token.IsSymbol('<'))
			{
				++_currentTemplateDepth;
			}
			else if (token.IsSymbol('>'))
			{
				if (_currentTemplateDepth == 0)
				{
					throw new UhtTokenException(_currentTokenReader!, token, "',' or ')'");
				}
				--_currentTemplateDepth;
			}
			return true;
		}
#endif

		private static UhtProperty? ResolveProperty(UhtPropertyResolvePhase resolvePhase,
			UhtPropertySettings propertySettings)
		{
			MethodInfo? methodInfo = typeof(UhtPropertyParser).GetMethod("ResolveProperty", BindingFlags.NonPublic | BindingFlags.Static);
			if (methodInfo == null)
			{
				throw new UhtException("Could not find method ResolveProperty in UhtPropertyParser");
			}

			object[] arguments = {resolvePhase, propertySettings};
			return (UhtProperty?)methodInfo.Invoke(null, arguments);
		}
		
		private static void RequireBitfield(IUhtTokenReader tokenReader, UhtPropertySettings propertySettings, ref UhtToken nameToken)
		{
			MethodInfo? methodInfo = typeof(UhtPropertyParser).GetMethod("RequireBitfield", BindingFlags.NonPublic | BindingFlags.Static);
			if (methodInfo == null)
			{
				throw new UhtException("Could not find method RequireBitfield in UhtPropertyParser");
			}

			object[] arguments = {tokenReader, propertySettings, nameToken};
			methodInfo.Invoke(null, arguments);
			nameToken = (UhtToken)arguments[2];
		}

		private static void RequireArray(IUhtTokenReader tokenReader, UhtPropertySettings propertySettings, ref UhtToken nameToken, char terminator)
		{
			MethodInfo? methodInfo = typeof(UhtPropertyParser).GetMethod("RequireArray", BindingFlags.NonPublic | BindingFlags.Static);
			if (methodInfo == null)
			{
				throw new UhtException("Could not find method RequireArray in UhtPropertyParser");
			}

			object[] arguments = {tokenReader, propertySettings, nameToken, terminator};
			methodInfo.Invoke(null, arguments);
			nameToken = (UhtToken)arguments[2];
		}
		
		private static bool CheckForOptionalParts(IUhtTokenReader tokenReader, UhtPropertySettings propertySettings, ref UhtToken nameToken)
		{
			MethodInfo? methodInfo = typeof(UhtPropertyParser).GetMethod("CheckForOptionalParts", BindingFlags.NonPublic | BindingFlags.Static);
			if (methodInfo == null)
			{
				throw new UhtException("Could not find method CheckForOptionalParts in UhtPropertyParser");
			}

			object[] arguments = {tokenReader, propertySettings, nameToken};
			bool gotOptionalParts = (bool) methodInfo.Invoke(null, arguments)!;
			nameToken = (UhtToken)arguments[2];
			return gotOptionalParts;
		}
	}
}