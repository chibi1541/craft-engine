#include "pch.h"
#include "AssetManager.h"
#include "Xml/XmlParser.h"
#include "Utils/FileUtils.h"
#include <filesystem>

NAME_SPACE_BEGIN(Craft)

namespace
{
	namespace fs = std::filesystem;

	// 데이터 실수를 잡을 때 원인을 남기고 죽는다.
	// (AnimStateMachineLoader.cpp의 CheckOrCrash와 같은 방식 - 게임이 콘솔 화면 버퍼를
	//  점유하고 있어서 printf는 안 보이고, OutputDebugStringA가 VS 출력 창에 뜬다)
	void CheckOrCrash(bool condition, const std::string& message)
	{
		if (condition)
		{
			return;
		}

		::OutputDebugStringA("[AssetManager] ");
		::OutputDebugStringA(message.c_str());
		::OutputDebugStringA("\n");

		ASSERT_CRASH(false);
	}

	std::string ToNarrow(const WCHAR* text)
	{
		return FileUtils::Convert(std::wstring(text));
	}
}

AssetManager* AssetManager::instance = nullptr;

AssetManager::AssetManager()
{
	ASSERT_CRASH(!instance);
	instance = this;
}

AssetManager::~AssetManager()
{
	instance = nullptr;
}

AssetManager& AssetManager::Get()
{
	ASSERT_CRASH(instance);
	return *instance;
}

void AssetManager::Tick(float deltaTime)
{
	for (auto& pair : caches)
	{
		pair.second->Tick(deltaTime, unloadThreshold);
	}
}

int AssetManager::LoadPrimaryAssetManifest(const WCHAR* manifestPath)
{
	const fs::path filePath{ manifestPath };

	// Primary 애셋은 필수 데이터라, 파일이 없는 것부터 이미 크래시감이다.
	CheckOrCrash(fs::exists(filePath) && fs::is_regular_file(filePath),
		"manifest not found: " + ToNarrow(manifestPath));

	XmlParser parser;
	XmlNode root;

	CheckOrCrash(parser.ParseFromFile(manifestPath, root) && root.IsValid(),
		"failed to parse manifest: " + ToNarrow(manifestPath));

	int loadedCount = 0;

	for (XmlNode& assetNode : root.FindChildren(L"Asset"))
	{
		const std::string typeName = ToNarrow(assetNode.GetStringAttr(L"type", L""));
		const std::string assetName = ToNarrow(assetNode.GetStringAttr(L"name", L""));
		const std::wstring assetPath = assetNode.GetStringAttr(L"path", L"");

		CheckOrCrash(!typeName.empty(), "<Asset> is missing 'type'");
		CheckOrCrash(!assetName.empty(), "<Asset> is missing 'name'");
		CheckOrCrash(!assetPath.empty(), "<Asset type=\"" + typeName + "\"> is missing 'path'");

		CheckOrCrash(primaryAssets.find(assetName) == primaryAssets.end(),
			"duplicate primary asset name '" + assetName + "'");

		auto factoryIt = primaryAssetFactories.find(typeName);
		CheckOrCrash(factoryIt != primaryAssetFactories.end(),
			"no RegisterPrimaryAssetType() call for type '" + typeName
			+ "' (asset '" + assetName + "')");

		std::shared_ptr<PrimaryDataAsset> asset = factoryIt->second();
		asset->name = assetName;
		asset->sourcePath = assetPath;

		XmlParser dataParser;
		XmlNode dataRoot;

		CheckOrCrash(dataParser.ParseFromFile(assetPath.c_str(), dataRoot) && dataRoot.IsValid(),
			"failed to parse data file for '" + assetName + "': " + ToNarrow(assetPath.c_str()));

		CheckOrCrash(asset->LoadFromXml(dataRoot),
			"LoadFromXml failed for '" + assetName + "' (type '" + typeName + "')");

		primaryAssets.emplace(assetName, asset);
		++loadedCount;
	}

	return loadedCount;
}

NAME_SPACE_END
