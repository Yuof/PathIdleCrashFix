// Fixes the crash at SkyrimSE+0CEC680 on 1.6.1170 (MovementMessagePlayIdle's constructor reading null + 0xE8).
// MovementAgentPathFollowerStandard state 14 = a start / stop / turn idle is prepared and waits; once the wait ends it
// enters PlayIdle with the prepared idle unchecked. The agent's SaveGame / LoadGame keep state 14 but not the idle, so
// an actor saved in that state crashes the game right after the load. States 13 and 15 check the same pointer; this
// gives state 14 that check: without its idle it is reset the way SetPath resets a prepared idle (drop it, state 1).
// Addresses come from Address Library; the field offsets were read from 1.6.1170 and 1.7.104 (same code there) and are
// confirmed at load by looking for the instructions that use them - a build where they differ stays inactive.

namespace
{
	constexpr std::ptrdiff_t kLock = 0x110;      // BSReadWriteLock taken by Update / SetPath / ClearPath
	constexpr std::ptrdiff_t kState = 0x118;     // std::uint32_t
	constexpr std::ptrdiff_t kIdleData = 0x130;  // smart pointer to the prepared idle
	constexpr std::ptrdiff_t kStateObj = 0x148;  // current state object
	constexpr std::uint32_t  kWaitIdle = 14;
	constexpr std::uint32_t  kFollowPath = 1;

	constexpr std::uint64_t kUpdate = 90711, kEnterPlayIdle = 90750, kLoadGame = 90739, kClearIdle = 90780;  // AE ids

	void (*ClearIdle)(void*) = nullptr;  // what SetPath calls before it sets state 1

	struct Expect
	{
		std::uint64_t             id;
		std::size_t               span;
		std::vector<std::uint8_t> bytes;
		std::string_view          what;
	};

	// the instructions that use the offsets above, identical in 1.6.1170 and 1.7.104
	// (cache\ingame\pathidle-20261025\selfcheck_verify.py runs the same check on both executables)
	const std::vector<Expect> kExpect{
		{ kUpdate, 0xA00, { 0x48, 0x81, 0xC1, 0xF8, 0x00, 0x00, 0x00 }, "Update: lock at +0x110"sv },
		{ kUpdate, 0xA00, { 0x49, 0x63, 0x86, 0x00, 0x01, 0x00, 0x00 }, "Update: state at +0x118"sv },
		{ kUpdate, 0xA00, { 0x49, 0x8B, 0x86, 0x18, 0x01, 0x00, 0x00 }, "Update: idle at +0x130"sv },
		{ kEnterPlayIdle, 0xB0, { 0x4C, 0x8D, 0x87, 0x30, 0x01, 0x00, 0x00 }, "PlayIdle entry: idle at +0x130"sv },
		{ kClearIdle, 0x90, { 0x48, 0x8B, 0x9F, 0x30, 0x01, 0x00, 0x00 }, "ClearIdle: idle at +0x130"sv },
		{ kLoadGame, 0x330, { 0x89, 0x85, 0x18, 0x01, 0x00, 0x00 }, "LoadGame: state at +0x118"sv },
	};

	std::span<const std::uint8_t> Code(std::uint64_t a_id, std::size_t a_span)
	{
		return { reinterpret_cast<const std::uint8_t*>(REL::ID(a_id).address()), a_span };
	}

	// state 14's handler: cmp [r14+0x148],0 / jne rel32 / lea rcx,[r14-0x18] / lea rdx,[rsp+imm8] / call EnterPlayIdle
	bool HasState14Handler()
	{
		constexpr std::array<std::uint8_t, 10> head{ 0x41, 0x83, 0xBE, 0x48, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x85 };
		constexpr std::array<std::uint8_t, 8>  tail{ 0x49, 0x8D, 0x4E, 0xE8, 0x48, 0x8D, 0x54, 0x24 };
		const auto code = Code(kUpdate, 0xA00);
		const auto target = REL::ID(kEnterPlayIdle).address();
		for (auto it = code.begin(); (it = std::search(it, code.end(), head.begin(), head.end())) != code.end(); ++it) {
			const auto rest = std::span{ it + head.size() + 4, code.end() };
			if (rest.size() < tail.size() + 6 || !std::equal(tail.begin(), tail.end(), rest.begin()) || rest[tail.size() + 1] != 0xE8) {
				continue;
			}
			const auto* call = rest.data() + tail.size() + 1;
			if (reinterpret_cast<std::uintptr_t>(call) + 5 + *reinterpret_cast<const std::int32_t*>(call + 1) == target) {
				return true;
			}
		}
		return false;
	}

	std::optional<std::string_view> SelfCheck()
	{
		for (const auto& e : kExpect) {
			const auto code = Code(e.id, e.span);
			if (std::search(code.begin(), code.end(), e.bytes.begin(), e.bytes.end()) == code.end()) {
				return e.what;
			}
		}
		if (!HasState14Handler()) {
			return "Update: state 14 entering PlayIdle"sv;
		}
		return std::nullopt;
	}

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
					ClearIdle(agent);
					At<std::uint32_t>(agent, kState) = kFollowPath;
					At<void*>(agent, kStateObj) = nullptr;
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
	const auto version = REL::Module::get().version();
	logger::info("{} v{} on {}"sv, Plugin::NAME, Plugin::VERSION.string(), version.string());
	// the ids above are AE ids: SE / VR number theirs differently, and a missing id stops the game in CommonLib's lookup
	if (!REL::Module::IsAE()) {
		logger::error("not an AE runtime: inactive"sv);
		return true;
	}
	if (const auto missing = SelfCheck()) {
		logger::error("self-check failed ({} not found in this build): inactive"sv, *missing);
		return true;
	}
	ClearIdle = reinterpret_cast<void (*)(void*)>(REL::ID(kClearIdle).address());
	REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_MovementAgentPathFollowerStandard[1] };
	Update::func = vtbl.write_vfunc(2, Update::thunk);
	logger::info("self-check passed, hooked MovementAgentPathFollowerStandard::Update"sv);
	return true;
}
