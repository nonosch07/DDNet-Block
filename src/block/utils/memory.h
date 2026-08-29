#ifndef BLOCK_UTILS_MEMORY_H
#define BLOCK_UTILS_MEMORY_H

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
	return [pWeakOwner, pMemberFunc](TArgs... Args) {
		if(auto pStrongOwner = pWeakOwner.lock())
		{
			(pStrongOwner.get()->*pMemberFunc)(std::forward<TArgs>(Args)...);
		}
	};
}
template<typename T, typename... TArgs>
std::function<void(TArgs...)> MakeSafeCallback(void (T::*pMemberFunc)(TArgs...), const std::weak_ptr<T> &pOwner)
{
	const std::weak_ptr<T> &pWeakOwner = std::move(pOwner);
	return [pWeakOwner, pMemberFunc](TArgs... Args) {
		if(auto pStrongOwner = pWeakOwner.lock())
		{
			(pStrongOwner.get()->*pMemberFunc)(std::forward<TArgs>(Args)...);
		}
	};
}

/**
 * CComponentAccessor is made to explicitly prohibit storing (copying) shared_ptr
 * However you can access weak_ptr to store it
 * This was done to remove chance of accidental copying shared_ptr of any component
 * As it will result in potentially infinite lifetime, when only ComponentRegistry owns and manages them
 * (unique_ptr adds too much pain in the ass, shared_ptr allows to make some mistakes in architecture)
 */
template<typename T>
class CComponentAccessor
{
	friend class CComponentRegistry;
	template<typename U>
	friend class CComponentAccessor;

public:
	explicit CComponentAccessor(std::shared_ptr<T> pPtr) :
		m_pPtr(std::move(pPtr)) {}
	CComponentAccessor(std::nullptr_t) noexcept :
		m_pPtr(nullptr) {} // lol

	CComponentAccessor(const CComponentAccessor &) = delete;
	CComponentAccessor &operator=(const CComponentAccessor &) = delete;

	CComponentAccessor(CComponentAccessor &&) noexcept = default;
	CComponentAccessor &operator=(CComponentAccessor &&) noexcept = default;

	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
	CComponentAccessor(CComponentAccessor<U> &&Other) noexcept :
		m_pPtr(std::move(Other.m_pPtr))
	{
	}

	template<typename U>
	CComponentAccessor<U> Cast() &&
	{
		return CComponentAccessor<U>(std::static_pointer_cast<U>(std::move(m_pPtr)));
	}

	T *operator->() const noexcept { return m_pPtr.get(); }
	T &operator*() const noexcept { return *m_pPtr; }

	explicit operator bool() const noexcept { return m_pPtr != nullptr; }

	std::weak_ptr<T> Store() const noexcept { return std::weak_ptr<T>(m_pPtr); }

private:
	std::shared_ptr<T> m_pPtr;
};

#endif // BLOCK_UTILS_MEMORY_H
