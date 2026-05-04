// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Text;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;

namespace FusionUbtPlugin.UHT.Parsers
{
	public class FusionPreResolveProperty : UhtPreResolveProperty
	{
#if UE_5_6_OR_LATER
		public FusionPreResolveProperty(UhtPropertySettings propertySettings, ReadOnlyMemory<UhtToken> typeTokens) : base(propertySettings)
		{
		}
#else
		public FusionPreResolveProperty(UhtPropertySettings propertySettings, ReadOnlyMemory<UhtToken> typeTokens) : base(propertySettings, typeTokens)
		{
		}
#endif
	}
}