#pragma once

#include "Utils/EngineMacro.h"
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>

NAME_SPACE_BEGIN(Craft)

// 타입 T를 파일 경로로부터 만들어내는 방법.
// AssetManager::RegisterLoader<T>()로 등록해두면 Load<T>()가 캐시 미스일 때 이걸로 채운다.
template<typename T>
using AssetLoaderFunc = std::function<std::shared_ptr<const T>(const WCHAR* path)>;

// 파일 경로로 애셋을 로드/캐싱하고, 오래 참조되지 않은 애셋은 자동으로 내리는 매니저.
// 언리얼의 Primary Asset / Asset Manager를 참고한 구조.
//
// - Load<T>(path) : 캐시에 있으면 즉시 반환, 없으면 등록된 로더로 로드해서 캐시에 넣고 반환.
// - 언로드 조건   : 매니저 말고 아무도 안 들고 있는 상태(shared_ptr 참조 1)가 unloadThreshold초
//                   이상 지속되면 내린다. Tick()에서 매 프레임 검사한다.
//
// 주의 - Load<T>()가 돌려준 shared_ptr을 호출부가 계속 들고 있어야 참조로 잡힌다.
// 반환값을 즉시 버리면(예: 내용물만 꺼내 쓰고 컨테이너는 버리는 경우) 다음 Tick부터
// 바로 유휴 판정이 시작되므로, 실제로 계속 쓰는 애셋이라면 멤버 변수 등으로 붙들고 있을 것.
class CRAFT_API AssetManager
{
	// 타입을 지우기 위한 인터페이스. caches 맵에 타입별 캐시를 이걸로 담는다.
	struct TypedCacheBase
	{
		virtual ~TypedCacheBase() = default;
		virtual void Tick(float deltaTime, float unloadThreshold) = 0;
	};

	template<typename T>
	struct TypedCache : TypedCacheBase
	{
		struct Entry
		{
			std::shared_ptr<const T> asset;
			float idleTime = 0.0f;
		};

		std::unordered_map<std::wstring, Entry> entries;
		AssetLoaderFunc<T> loader;

		virtual void Tick(float deltaTime, float unloadThreshold) override
		{
			// Level::ProcessAddAndDestoryActors()와 동일한 순회-중-삭제 패턴.
			for (auto it = entries.begin(); it != entries.end(); )
			{
				Entry& entry = it->second;

				// 매니저(entry.asset) 말고 다른 곳에서도 들고 있으면 사용 중 - 유휴 시간 리셋.
				if (entry.asset.use_count() > 1)
				{
					entry.idleTime = 0.0f;
				}
				else
				{
					entry.idleTime += deltaTime;
				}

				if (entry.idleTime >= unloadThreshold)
				{
					it = entries.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	};

public:
	AssetManager();
	~AssetManager();

	// cache가 unique_ptr의 map 이므로 복사 대입 처리 delete 안해주면 컴파일 에러 발생...
	AssetManager(const AssetManager&) = delete;
	AssetManager& operator=(const AssetManager&) = delete;

	// Input::Get()/Renderer::Get()과 동일한 자기-싱글턴 패턴.
	static AssetManager& Get();

	// 타입 T를 로드할 방법을 등록한다. 보통 엔진 초기화 시점에 한 번씩.
	template<typename T>
	void RegisterLoader(AssetLoaderFunc<T> loader)
	{
		GetOrCreateCache<T>().loader = loader;
	}

	// 캐시에 있으면 반환, 없으면 등록된 로더로 로드 후 캐시에 넣고 반환.
	// 등록된 로더가 없으면 크래시한다 - 로더 누락은 런타임 상황이 아니라 코드 실수이기 때문.
	template<typename T>
	std::shared_ptr<const T> Load(const WCHAR* path)
	{
		TypedCache<T>& cache = GetOrCreateCache<T>();

		const std::wstring key(path);
		auto it = cache.entries.find(key);

		if (it != cache.entries.end())
		{
			return it->second.asset;
		}

		ASSERT_CRASH(cache.loader);

		typename TypedCache<T>::Entry entry;
		entry.asset = cache.loader(path);

		cache.entries.emplace(key, entry);

		return entry.asset;
	}

	// 매 프레임 호출. 유휴 시간을 누적하고, 조건을 만족한 항목을 정리한다.
	void Tick(float deltaTime);

	// 유휴 판정 임계값(초). 기본 30초.
	inline void SetUnloadThreshold(float seconds) { unloadThreshold = seconds; }
	inline float GetUnloadThreshold() const { return unloadThreshold; }

private:
	// 템플릿 멤버 함수라 정의를 여기(헤더)에 둬야 함 - cpp로 분리 불가.
	template<typename T>
	TypedCache<T>& GetOrCreateCache()
	{
		const std::type_index key = std::type_index(typeid(T));
		auto it = caches.find(key);

		if (it != caches.end())
		{
			return static_cast<TypedCache<T>&>(*it->second);
		}

		auto newCache = std::make_unique<TypedCache<T>>();
		TypedCache<T>& ref = *newCache;
		caches.emplace(key, std::move(newCache));

		return ref;
	}

private:
	std::unordered_map<std::type_index, std::unique_ptr<TypedCacheBase>> caches;

	float unloadThreshold = 30.0f;

	static AssetManager* instance;
};

NAME_SPACE_END
