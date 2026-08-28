#include "pch.h"
#include "AssetManager.h"
#include "Job/JobQueue.h"
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

	// JobQueue가 enable_shared_from_this를 상속해서 shared_ptr로 만들어야 한다.
	loadQueue = std::make_shared<JobQueue>();
	completionQueue = std::make_shared<JobQueue>();
}

AssetManager::~AssetManager()
{
	// 워커가 살아있는 채로 파괴되면 워커의 잡이 죽은 this를 만진다.
	// Engine::Shutdown()이 먼저 멈추지만, 그 경로를 안 타는 경우를 위한 안전망이다.
	StopWorkers();

	instance = nullptr;
}

void AssetManager::EnqueueLoadJob(std::function<void()> job)
{
	loadQueue->DoAsync(std::move(job));
}

void AssetManager::EnqueueCompletionJob(std::function<void()> job)
{
	completionQueue->DoAsync(std::move(job));
}

void AssetManager::StopWorkers()
{
	isRunning.store(false);
}

void AssetManager::WorkerLoop()
{
	while (isRunning.load())
	{
		// 쌓인 파싱 잡을 전부 소비하고 돌아온다.
		loadQueue->Execute();

		// Lock이 condition_variable이 아니라 스핀락이라
		// 빈 큐를 계속 돌리면 코어 하나를 그냥 태운다.
		// 애셋 로딩에서 1ms 지연은 의미가 없고 그동안 CPU는 0%다.
		::Sleep(1);
	}
}

AssetManager& AssetManager::Get()
{
	ASSERT_CRASH(instance);
	return *instance;
}

void AssetManager::Tick(float deltaTime)
{
	// 워커가 넘긴 완료 잡을 여기서 처리한다.
	// = 캐시 삽입 + 완료 콜백 호출. 전부 메인 쓰레드다.
	//
	// 유휴 정리보다 먼저 돌려야 이번 프레임에 도착한 애셋이
	// 곧바로 유휴 판정을 받는 일이 없다.
	completionQueue->Execute();

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
