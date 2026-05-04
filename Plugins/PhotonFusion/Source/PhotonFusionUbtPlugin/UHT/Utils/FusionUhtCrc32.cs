// Copyright 2026 Exit Games GmbH. All Rights Reserved.

namespace FusionUbtPlugin.UHT.Utils
{
	public static class FusionUhtCrc32
	{
		private static readonly uint[] Table;

		static FusionUhtCrc32()
		{
			const uint poly = 0xEDB88320u;
			Table = new uint[256];
			for (uint i = 0; i < Table.Length; ++i)
			{
				uint crc = i;
				for (int j = 0; j < 8; j++)
					crc = (crc & 1) != 0 ? (crc >> 1) ^ poly : crc >> 1;
				Table[i] = crc;
			}
		}

		public static uint Compute(string text)
		{
			uint crc = 0xFFFFFFFFu;
			foreach (char c in text)
			{
				byte b = (byte)c;
				crc = (crc >> 8) ^ Table[(crc ^ b) & 0xFF];
			}

			return ~crc;
		}

	}
}