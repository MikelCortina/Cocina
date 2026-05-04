// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace FusionUbtPlugin.UHT.Utils
{
	struct FusionUHTParameterHandler
	{
		private readonly UhtMetaData _metaData;
		private readonly string[]? _parameterNames;
		private readonly int _numberLeaveUnmarked;
		private readonly bool _bUseNumber;
		private int _alreadyLeft;

		public FusionUHTParameterHandler(UhtMetaData metaData)
		{
			_metaData = metaData;
			_parameterNames = null;
			_numberLeaveUnmarked = -1;
			_alreadyLeft = 0;
			_bUseNumber = false;

			if (_metaData.TryGetValue(UhtNames.AdvancedDisplay, out string? foundString))
			{
				_parameterNames = foundString.ToString().Split(',', StringSplitOptions.RemoveEmptyEntries);
				for (int index = 0, endIndex = _parameterNames.Length; index < endIndex; ++index)
				{
					_parameterNames[index] = _parameterNames[index].Trim();
				}
				if (_parameterNames.Length == 1)
				{
					_bUseNumber = Int32.TryParse(_parameterNames[0], out _numberLeaveUnmarked);
				}
			}
		}

		/**
		 * return if given parameter should be marked as Advance View,
		 * the function should be called only once for any parameter
		 */
		public bool ShouldMarkParameter(StringView parameterName)
		{
			if (_bUseNumber)
			{
				if (_numberLeaveUnmarked < 0)
				{
					return false;
				}
				if (_alreadyLeft < _numberLeaveUnmarked)
				{
					++_alreadyLeft;
					return false;
				}
				return true;
			}

			if (_parameterNames == null)
			{
				return false;
			}

			foreach (string element in _parameterNames)
			{
				if (parameterName.Span.Equals(element, StringComparison.OrdinalIgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		/** return if more parameters can be marked */
		public readonly bool CanMarkMore()
		{
			return _bUseNumber ? _numberLeaveUnmarked > 0 : (_parameterNames != null && _parameterNames.Length > 0);
		}
	}
}