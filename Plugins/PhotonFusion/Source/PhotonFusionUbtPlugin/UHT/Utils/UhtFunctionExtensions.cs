// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace FusionUbtPlugin.UHT.Utils
{
	public static class UhtFunctionExtensions
	{
		public static UhtPropertyParseOptions ExternalGetPropertyParseOptions(this UhtFunction function, bool returnValue)
		{
			switch (function.FunctionType)
			{
				case UhtFunctionType.Delegate:
				case UhtFunctionType.SparseDelegate:
					return (returnValue ? UhtPropertyParseOptions.None : UhtPropertyParseOptions.CommaSeparatedName) | UhtPropertyParseOptions.DontAddReturn;

				case UhtFunctionType.Function:
					UhtPropertyParseOptions options = UhtPropertyParseOptions.DontAddReturn; // Fetch the function name
					options |= returnValue ? UhtPropertyParseOptions.FunctionNameIncluded : UhtPropertyParseOptions.NameIncluded;
					if (function.FunctionFlags.HasAllFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.Native))
					{
						options |= UhtPropertyParseOptions.NoAutoConst;
					}
					return options;

				default:
					throw new UhtIceException("Unknown enumeration value");
			}
		}
	}
}