// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Text;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using FusionUbtPlugin.UHT.Generators;

namespace FusionUbtPlugin.UHT.Utils
{
	internal struct FusionMacroCreator : IDisposable
	{
		private readonly StringBuilder _builder;
		private readonly int _startingLength;

		public FusionMacroCreator(StringBuilder builder, FusionHeaderCodeGeneratorHFile generator, int lineNumber, string macroSuffix)
		{
			builder.Append("#define ").AppendMacroName(generator, lineNumber, macroSuffix).Append(" ");
			_builder = builder;
			_startingLength = builder.Length;
		}

		public FusionMacroCreator(StringBuilder builder, FusionHeaderCodeGeneratorHFile generator, UhtClass classObj, string macroSuffix)
		{
			builder.Append("#define ").AppendMacroName(generator, classObj, macroSuffix).Append(" \\\r\n");
			_builder = builder;
			_startingLength = builder.Length;
		}

		public FusionMacroCreator(StringBuilder builder, FusionHeaderCodeGeneratorHFile generator, UhtScriptStruct scriptStruct, string macroSuffix)
		{
			builder.Append("#define ").AppendMacroName(generator, scriptStruct, macroSuffix).Append(" \\\r\n");
			_builder = builder;
			_startingLength = builder.Length;
		}

		public FusionMacroCreator(StringBuilder builder, FusionHeaderCodeGeneratorHFile generator, UhtFunction function, string macroSuffix)
		{
			builder.Append("#define ").AppendMacroName(generator, function, macroSuffix).Append(" \\\r\n");
			_builder = builder;
			_startingLength = builder.Length;
		}

		public void Dispose()
		{
			int finalLength = _builder.Length;
			if (finalLength < 4 ||
			    _builder[finalLength - 4] != ' ' ||
			    _builder[finalLength - 3] != '\\' ||
			    _builder[finalLength - 2] != '\r' ||
			    _builder[finalLength - 1] != '\n')
			{
				throw new UhtException("Macro line must end in ' \\\\\\r\\n'");
			}

			_builder.Length -= 4;
			if (finalLength == _startingLength)
			{
				_builder.Append("\r\n");
			}
			else
			{
				_builder.Append("\r\n\r\n\r\n");
			}
		}
	}

}