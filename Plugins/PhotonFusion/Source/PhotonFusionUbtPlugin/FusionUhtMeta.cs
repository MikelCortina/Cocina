// Copyright 2026 Exit Games GmbH. All Rights Reserved.

using System;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace FusionUbtPlugin
{
    internal static class FusionUhtMeta
    {
        private const string PropertyMeta           = "QPropertyName";
        private const string PropertyGetterNameMeta = "QPropertyGetterName";
        private const string PropertySetterNameMeta = "QPropertySetterName";
        private const string GeneratedBodyLineNumberMeta  = "QGeneratedBodyLineNumber";
        private const string ReplicateMeta                = "QReplicate";
        public const  string QStateTypeMeta                = "QStateType";
        public const  string QPropertySingleModeMeta       = "QStateRoot";
        

        public static bool TryGetBodyLineNumber(this UhtStruct classObj, out int lineNumber)
        {
            if (!classObj.MetaData.TryGetValue(GeneratedBodyLineNumberMeta, out string? strValue))
            {
                lineNumber = 0;
                return false;
            }
            
            lineNumber = Int32.Parse(strValue);
            return true;
        }

        // public static void AddQReplicatedState(this UhtStruct classObj, string stateType)
        // {
        //     classObj.MetaData.Add($"{QReplicateMeta}", $"{stateType}");
        // }
        
        public static void AddQReplicatedField(this UhtStruct classObj, string fieldType, string fieldName)
        {
            // this prevents UE codegen to work!
#if false
            int fieldIndex = 0;
            while (classObj.MetaData.TryGetValue(QReplicateFieldsMeta, fieldIndex, out _))
            {
                ++fieldIndex;
            }
            classObj.MetaData.Add(QReplicateFieldsMeta, fieldIndex, $"{fieldType}:{fieldName}");
#else
            int fieldIndex = 0;
            while (classObj.MetaData.TryGetValue($"{ReplicateMeta}{fieldIndex}", out _))
            {
                ++fieldIndex;
            }
            classObj.MetaData.Add($"{ReplicateMeta}{fieldIndex}", $"{fieldType}@{fieldName}");
#endif
        }

        // public static bool TryGetQReplicatedState(this UhtStruct classObj, out string? stateType)
        // {
        //     if (!classObj.MetaData.TryGetValue($"{QReplicateMeta}", out string? value))
        //     {
        //         stateType = null;
        //         return false;
        //     }
        //     
        //     stateType = value;
        //     return true;
        // }
        
        public static bool TryGetQReplicatedField(this UhtStruct classObj, int index, out string? fieldType, out string? fieldName)
        {
            if (!classObj.MetaData.TryGetValue($"{ReplicateMeta}{index}", out string? value))
            {
                fieldType = null;
                fieldName = null;
                return false;
            }
            
            string[] parts = value.Split('@');
            if (parts.Length != 2)
            {
                throw new InvalidOperationException($"Expected 2 parts in {value}");
            }
            fieldType = parts[0];
            fieldName = parts[1];
            return true;
        }
        
        public static void SetQGeneratedBodyLineNumber(this UhtStruct classObj, int lineNumber)
        {
            classObj.MetaData.Add(GeneratedBodyLineNumberMeta, lineNumber.ToString());
        }
        
        public static bool TryGetQPropertyNiceName(this UhtProperty property, out string? niceName)
        {
            return property.MetaData.TryGetValue(PropertyMeta, out niceName);
        }
        
        public static string GetQPropertyNiceName(this UhtProperty property)
        {
            if (property.MetaData.TryGetValue(PropertyMeta, out string? niceName))
            {
                return niceName;
            }

            throw new UhtException($"No {PropertyMeta} meta for property {property.SourceName}");
        }
        
        public static void SetQPropertyNiceName(this UhtProperty property, string niceName)
        {
            property.MetaData.Add(PropertyMeta, niceName);
        }
        
        public static bool TryGetQPropertyGetterName(this UhtProperty property, out string? getterName)
        {
            return property.MetaData.TryGetValue(PropertyGetterNameMeta, out getterName);
        }
        
        public static void SetQPropertyGetterName(this UhtProperty property, string getterName)
        {
            property.MetaData.Add(PropertyGetterNameMeta, getterName);
        }
        
        public static bool TryGetQPropertySetterName(this UhtProperty property, out string? setterName)
        {
            return property.MetaData.TryGetValue(PropertySetterNameMeta, out setterName);
        }
        
        public static void SetQPropertySetterName(this UhtProperty property, string setterName)
        {
            property.MetaData.Add(PropertySetterNameMeta, setterName);
        }
        
        public static bool IsQStateRoot(this UhtProperty property)
        {
            return property.MetaData.ContainsKey("QStateRoot");
        }

        public static bool TryGetQPropertyStateTypeOverride(this UhtProperty property, out string? result)
        {
	        return property.MetaData.TryGetValue(QStateTypeMeta, out result);
        }

        public static void SetQPropertyStateTypeOverride(this UhtProperty property, string stateType)
        {
	        property.MetaData.Add(QStateTypeMeta, stateType);
        }
        
        
    }
}