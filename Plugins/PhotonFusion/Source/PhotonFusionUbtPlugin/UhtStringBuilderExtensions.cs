// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace FusionUbtPlugin
{
    public static class UhtStringBuilderExtensions
    {
        public static StringBuilder AppendType(this StringBuilder stringBuilder, UhtProperty property)
        {
            return property.AppendText(stringBuilder, UhtPropertyTextType.Generic);
        }
        
        public static StringBuilder AppendRetType(this StringBuilder stringBuilder, UhtProperty property)
        {
	        return property.AppendText(stringBuilder, UhtPropertyTextType.GenericFunctionArgOrRetVal);
        }
        
        public static StringBuilder AppendName(this StringBuilder stringBuilder, UhtProperty property)
        {
            return stringBuilder.Append(property.SourceName);
        }
        
        public static string GetFileId(this UhtHeaderFile headerFile)
        {
            string filePath   = headerFile.FilePath;
            
            UhtSession session = headerFile.Session;
            
            bool   isRelative = !Path.IsPathRooted(filePath);
            if (!isRelative && session.EngineDirectory != null)
            {
                string? directory = Path.GetDirectoryName(session.EngineDirectory);
                if (!String.IsNullOrEmpty(directory))
                {
                    filePath   = Path.GetRelativePath(directory, filePath);
                    isRelative = !Path.IsPathRooted(filePath);
                }
            }
            if (!isRelative && session.ProjectDirectory != null)
            {
                string? directory = Path.GetDirectoryName(session.ProjectDirectory);
                if (!String.IsNullOrEmpty(directory))
                {
                    filePath   = Path.GetRelativePath(directory, filePath);
                    isRelative = !Path.IsPathRooted(filePath);
                }
            }
            filePath = filePath.Replace('\\', '/');
            if (isRelative)
            {
                while (filePath.StartsWith("../", StringComparison.Ordinal))
                {
                    filePath = filePath[3..];
                }
            }

            char[] outFilePath = new char[filePath.Length + 4];
            outFilePath[0] = 'F';
            outFilePath[1] = 'I';
            outFilePath[2] = 'D';
            outFilePath[3] = '_';
            for (int index = 0; index < filePath.Length; ++index)
            {
                outFilePath[index + 4] = UhtFCString.IsAlnum(filePath[index]) ? filePath[index] : '_';
            }
            
            return new string(outFilePath);
        }
    }
}