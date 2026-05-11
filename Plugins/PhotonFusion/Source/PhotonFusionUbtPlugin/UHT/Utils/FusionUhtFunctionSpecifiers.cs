// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Types;

namespace FusionUbtPlugin.UHT.Utils
{
	[UnrealHeaderTool]
	[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
	[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
	public static class FusionUhtFunctionSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void TargetMasterClientSpecifier(UhtSpecifierContext specifierContext, StringView? value)
		{
			if (specifierContext.Type is FusionRPCFunction function)
			{
				function.Target = FusionRPCTarget.TargetMasterClient;
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void TargetAllClientsSpecifier(UhtSpecifierContext specifierContext, StringView? value)
		{
			if (specifierContext.Type is FusionRPCFunction function)
			{
				function.Target = FusionRPCTarget.TargetAllClients;
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void TargetObjectOwnerSpecifier(UhtSpecifierContext specifierContext, StringView? value)
		{
			if (specifierContext.Type is FusionRPCFunction function)
			{
				function.Target = FusionRPCTarget.TargetObjectOwner;
			}
		}

		[UhtSpecifier(Extends = UhtTableNames.Function, ValueType = UhtSpecifierValueType.OptionalString)]
		private static void TargetEveryoneElseSpecifier(UhtSpecifierContext specifierContext, StringView? value)
		{
			if (specifierContext.Type is FusionRPCFunction function)
			{
				function.Target = FusionRPCTarget.TargetEveryoneElse;
			}
		}
	}
}