// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Fusion/LogUtils.h"
#include "Fusion/StringType.h"
#include "Containers/StringConv.h"

DECLARE_LOG_CATEGORY_EXTERN(LogFusion, Log, All);

#define FUSION_LOG_TRACE(txt, ...) \
do { \
FString _fusionLogTempStr = FString::Printf(TEXT(txt), ##__VA_ARGS__); \
const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> _fusionLogConverted = StringCast<UTF8CHAR>(*_fusionLogTempStr); \
PhotonCommon::Log(PhotonCommon::LogLevel::Trace, reinterpret_cast<const PhotonCommon::CharType*>(_fusionLogConverted.Get())); \
} while (false)

#define FUSION_LOG_DEBUG(txt, ...) \
do { \
FString _fusionLogTempStr = FString::Printf(TEXT(txt), ##__VA_ARGS__); \
const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> _fusionLogConverted = StringCast<UTF8CHAR>(*_fusionLogTempStr); \
PhotonCommon::Log(PhotonCommon::LogLevel::Debug, reinterpret_cast<const PhotonCommon::CharType*>(_fusionLogConverted.Get())); \
} while (false)

#define FUSION_LOG(txt, ...) \
do { \
FString _fusionLogTempStr = FString::Printf(TEXT(txt), ##__VA_ARGS__); \
const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> _fusionLogConverted = StringCast<UTF8CHAR>(*_fusionLogTempStr); \
PhotonCommon::Log(PhotonCommon::LogLevel::Info, reinterpret_cast<const PhotonCommon::CharType*>(_fusionLogConverted.Get())); \
} while (false)

#define FUSION_LOG_WARN(txt, ...) \
do { \
FString _fusionLogTempStr = FString::Printf(TEXT(txt), ##__VA_ARGS__); \
const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> _fusionLogConverted = StringCast<UTF8CHAR>(*_fusionLogTempStr); \
PhotonCommon::Log(PhotonCommon::LogLevel::Warning, reinterpret_cast<const PhotonCommon::CharType*>(_fusionLogConverted.Get())); \
} while (false)

#define FUSION_LOG_ERROR(txt, ...) \
do { \
FString _fusionLogTempStr = FString::Printf(TEXT(txt), ##__VA_ARGS__); \
const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> _fusionLogConverted = StringCast<UTF8CHAR>(*_fusionLogTempStr); \
PhotonCommon::Log(PhotonCommon::LogLevel::Error, reinterpret_cast<const PhotonCommon::CharType*>(_fusionLogConverted.Get())); \
} while (false)

class FPhotonFusion : public IModuleInterface
{
public:

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void OnBeginPIE(bool bArg);
	void OnEndPIE(bool bArg);
};
