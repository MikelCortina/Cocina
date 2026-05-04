// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using FusionUbtPlugin.UHT.Types;

namespace FusionUbtPlugin.UHT.Utils
{
	public static class FusionUhtFunctionSpecifiers
	{
		public static void TargetMasterClient(UhtSpecifierContext specifierContext, StringView? value)
		{
			FusionRPCFunction? function = specifierContext.Type as FusionRPCFunction;
			if (function != null)
			{
				function.Target = FusionRPCTarget.TargetMasterClient;
			}
        }
		
		public static void TargetAllClients(UhtSpecifierContext specifierContext, StringView? value)
		{
			FusionRPCFunction? function = specifierContext.Type as FusionRPCFunction;
			if (function != null)
			{
				function.Target = FusionRPCTarget.TargetAllClients;
			}
		}
		
		public static void TargetObjectOwner(UhtSpecifierContext specifierContext, StringView? value)
		{
			FusionRPCFunction? function = specifierContext.Type as FusionRPCFunction;
			if (function != null)
			{
				function.Target = FusionRPCTarget.TargetObjectOwner;
			}
		}
		public static void TargetEveryoneElse(UhtSpecifierContext specifierContext, StringView? value)
		{
			FusionRPCFunction? function = specifierContext.Type as FusionRPCFunction;
			if (function != null)
			{
				function.Target = FusionRPCTarget.TargetEveryoneElse;
			}
		}
	}
}