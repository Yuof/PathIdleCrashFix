// Fixes the crash at SkyrimSE+0CEC680 (MovementMessagePlayIdle's constructor reading null + 0xE8).
// MovementAgentPathFollowerStandard state 14 = a start / stop / turn idle is prepared and waits; once the wait ends it
// enters PlayIdle with the prepared idle unchecked. The agent's SaveGame / LoadGame keep state 14 but not the idle, so
// an actor saved in that state crashes the game right after the load. States 13 and 15 check the same pointer; this
// gives state 14 that check: without its idle it goes back to state 1, as when a new path replaces a prepared idle.
// Offsets read from 1.6.1170: Update 90711, LoadGame 90739, SetPath 90713.

namespace
{
	constexpr std::ptrdiff_t kLock = 0x110;      // BSReadWriteLock taken by Update / SetPath / ClearPath
	constexpr std::ptrdiff_t kState = 0x118;     // std::uint32_t
	constexpr std::ptrdiff_t kIdleData = 0x130;  // smart pointer to the prepared idle
	constexpr std::uint32_t  kWaitIdle = 14;
	constexpr std::uint32_t  kFollowPath = 1;

	template <class T>
	T& At(void* a_agent, std::ptrdiff_t a_off)
	{
		return *reinterpret_cast<T*>(static_cast<std::byte*>(a_agent) + a_off);
	}

	bool LostIdle(void* a_agent) { return At<std::uint32_t>(a_agent, kState) == kWaitIdle && !At<void*>(a_agent, kIdleData); }

	struct Update  // vfunc 2 of the agent's +0x18 subobject
	{
		static std::uintptr_t thunk(void* a_sub, void* a_2, void* a_3)
		{
			auto* agent = static_cast<std::byte*>(a_sub) - 0x18;
			if (LostIdle(agent)) {
				auto& lock = At<RE::BSReadWriteLock>(agent, kLock);
				lock.LockForWrite();
				const bool hit = LostIdle(agent);
				if (hit) {
					At<std::uint32_t>(agent, kState) = kFollowPath;
				}
				lock.UnlockForWrite();
				if (hit) {
					logger::info("path follower {:X}: state 14 without its idle -> state 1"sv, reinterpret_cast<std::uintptr_t>(agent));
				}
			}
			return func(a_sub, a_2, a_3);
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};
}

SKSEPluginInfo(
	.Version = Plugin::VERSION,
	.Name = Plugin::NAME,
	.Author = "local"sv,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true });
	logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());
	if (REL::Module::get().version() != REL::Version{ 1, 6, 1170, 0 }) {  // ponytail: offsets checked on 1.6.1170 only
		logger::error("runtime {} not supported (offsets read from 1.6.1170): inactive"sv, REL::Module::get().version().string());
		return true;
	}
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MovementAgentPathFollowerStandard[1] };
	Update::func = vtbl.write_vfunc(2, Update::thunk);
	logger::info("hooked MovementAgentPathFollowerStandard::Update"sv);
	return true;
}
