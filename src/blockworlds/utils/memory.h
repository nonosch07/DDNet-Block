#ifndef BLOCKWORLDS_UTILS_MEMORY_H
#define BLOCKWORLDS_UTILS_MEMORY_H

#include <functional>
#include <memory>

/**
 * Creates safe callback with context object.
 * Callback will be called only if owner still exists.
 *
 * @tparam T Owner class type.
 * @tparam TArgs Args types.
 * @param pMemberFunc Class member pointer.
 * @param pOwner Smart pointer to owner instance.
 * @return std::function, that can be safely stored and called.
 */
template<typename T, typename... TArgs>
std::function<void(TArgs...)> MakeSafeCallback(void (T::*pMemberFunc)(TArgs...), std::shared_ptr<T> pOwner)
{
	std::weak_ptr<T> pWeakOwner = pOwner;
	return [pWeakOwner, pMemberFunc](TArgs... args) {
		if (auto pStrongOwner = pWeakOwner.lock())
		{
			(pStrongOwner.get()->*pMemberFunc)(std::forward<TArgs>(args)...);
		}
	};
}

#endif // BLOCKWORLDS_UTILS_MEMORY_H
