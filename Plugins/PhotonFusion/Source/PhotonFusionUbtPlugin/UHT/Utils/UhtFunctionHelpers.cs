// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Parsers;

namespace FusionUbtPlugin.UHT.Utils
{
	internal static class UhtFunctionHelpers
	{
		  
		internal static void AddFunctionToParentScope(UhtFunction function)
		{
			function.Outer?.AddChild(function);
		}

		internal static void SetFunctionNames(UhtFunction function)
		{
			// The source name won't have the suffix applied to delegate names, however, the engine name will
			// We use the engine name because we need to detect the suffix for delegates
			string functionName = function.EngineName;
			if (functionName.EndsWith(UhtFunction.GeneratedDelegateSignatureSuffix, StringComparison.Ordinal))
			{
				functionName = functionName[..^UhtFunction.GeneratedDelegateSignatureSuffix.Length];
			}

			function.UnMarshalAndCallName = "exec" + functionName;

			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent))
			{
				function.MarshalAndCallName = functionName;
				if (function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
				{
					function.CppImplName = function.EngineName + "_Implementation";
				}
			}
			else if (function.FunctionFlags.HasAllFlags(EFunctionFlags.Native | EFunctionFlags.Net))
			{
				function.MarshalAndCallName = functionName;
				if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse))
				{
					// Response function implemented by programmer and called directly from thunk
					function.CppImplName = function.EngineName;
				}
				else
				{
					if (function.CppImplName.Length == 0)
					{
						function.CppImplName = function.EngineName + "_Implementation";
					}
					else if (function.CppImplName == functionName)
					{
						function.LogError("Native implementation function must be different than original function name.");
					}

					if (function.CppValidationImplName.Length == 0 && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
					{
						function.CppValidationImplName = function.EngineName + "_Validate";
					}
					else if (function.CppValidationImplName == functionName)
					{
						function.LogError("Validation function must be different than original function name.");
					}
				}
			}
			else if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
			{
				function.MarshalAndCallName = "delegate" + functionName;
			}

			if (function.CppImplName.Length == 0)
			{
				function.CppImplName = functionName;
			}

			if (function.MarshalAndCallName.Length == 0)
			{
				function.MarshalAndCallName = "event" + functionName;
			}
		}
		
		internal static void ParseParameterList(UhtParsingScope topScope, FusionPropertyParser propertyParser)
		{
			UhtFunction function = (UhtFunction)topScope.ScopeType;

			UhtPropertyParseOptions options = function.ExternalGetPropertyParseOptions(false);

			bool isNetFunc = function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net);
			UhtPropertyCategory propertyCategory = isNetFunc ? UhtPropertyCategory.ReplicatedParameter : UhtPropertyCategory.RegularParameter;
			EPropertyFlags disallowFlags = ~(EPropertyFlags.ParmFlags | EPropertyFlags.AutoWeak | EPropertyFlags.RepSkip | EPropertyFlags.UObjectWrapper | EPropertyFlags.NativeAccessSpecifiers);

			FusionUHTParameterHandler advancedDisplay = new(topScope.ScopeType.MetaData);

			topScope.TokenReader.RequireList(')', ',', false, () =>
			{
				propertyParser.Parse(topScope, disallowFlags, options, propertyCategory,
					(UhtParsingScope topScope, UhtProperty property, ref UhtToken nameToken, UhtLayoutMacroType layoutMacroType) =>
					{
						property.PropertyFlags |= EPropertyFlags.Parm;
						if (advancedDisplay.CanMarkMore() && advancedDisplay.ShouldMarkParameter(property.EngineName))
						{
							property.PropertyFlags |= EPropertyFlags.AdvancedDisplay;
						}

						// Default value.
						if (topScope.TokenReader.TryOptional('='))
						{
							List<UhtToken> defaultValueTokens = new();
							int parenthesisNestCount = 0;
							while (!topScope.TokenReader.IsEOF)
							{
								UhtToken token = topScope.TokenReader.PeekToken();
								if (token.IsSymbol(','))
								{
									if (parenthesisNestCount == 0)
									{
										break;
									}
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
								else if (token.IsSymbol(')'))
								{
									if (parenthesisNestCount == 0)
									{
										break;
									}
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
									--parenthesisNestCount;
								}
								else if (token.IsSymbol('('))
								{
									++parenthesisNestCount;
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
								else
								{
									defaultValueTokens.Add(token);
									topScope.TokenReader.ConsumeToken();
								}
							}

							// allow exec functions to be added to the metaData, this is so we can have default params for them.
							bool storeCppDefaultValueInMetaData = function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintCallable | EFunctionFlags.Exec);
							if (defaultValueTokens.Count > 0 && storeCppDefaultValueInMetaData)
							{
								property.DefaultValueTokens = defaultValueTokens;
							}
						}
					});
			});
		}
	}
}