#ifndef BLOCKWORLDS_UTILS_MEMORY_H
#define BLOCKWORLDS_UTILS_MEMORY_H

#include <functional>
#include <memory>
#include <utility>

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
template<typename T, typename... TArgs>
std::function<void(TArgs...)> MakeSafeCallback(void (T::*pMemberFunc)(TArgs...), std::weak_ptr<T> pOwner)
{
	std::weak_ptr<T> pWeakOwner = pOwner;
	return [pWeakOwner, pMemberFunc](TArgs... args) {
		if (auto pStrongOwner = pWeakOwner.lock())
		{
			(pStrongOwner.get()->*pMemberFunc)(std::forward<TArgs>(args)...);
		}
	};
}

/**
 * ComponentAccessor is made to explicitly prohibit storing (copying) it
 * However you can access weak_ptr to store it
 * This was done to remove chance of accidental storing of shared_ptr to any component
 * As it will result in potentially infinite lifetime, when only ComponentRegistry owns and manages them
 * (unique_ptr adds too much pain in the ass, shared_ptr allows to make some mistakes in architecture)
 */
template<typename T>
class ComponentAccessor
{
	friend class CComponentRegistry;
public:
	explicit ComponentAccessor(std::shared_ptr<T> pPtr) : m_pPtr(std::move(pPtr)) {}
	ComponentAccessor(std::nullptr_t) noexcept : m_pPtr(nullptr) {} // lol

	ComponentAccessor(const ComponentAccessor&) = delete;
	ComponentAccessor& operator=(const ComponentAccessor&) = delete;

	ComponentAccessor(ComponentAccessor&&) noexcept = default;
	ComponentAccessor& operator=(ComponentAccessor&&) noexcept = default;

	T* operator->() const noexcept { return m_pPtr.get(); }
	T& operator*() const noexcept { return *m_pPtr; }

	explicit operator bool() const noexcept { return m_pPtr != nullptr; }

	std::weak_ptr<T> Store() const noexcept { return std::weak_ptr<T>(m_pPtr); }

private:
	std::shared_ptr<T> m_pPtr;
};

#endif // BLOCKWORLDS_UTILS_MEMORY_H
