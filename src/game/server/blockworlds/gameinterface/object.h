#ifndef GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_OBJECT_H
#define GAME_SERVER_BLOCKWORLDS_GAMEINTERFACE_OBJECT_H

#include <cstdio>
#include <list>
#include <utility>

#include <engine/shared/memheap.h>

class IGameInterfaceRenderer;

class CGenericTreeElementInfo
{
	int m_Depth;
	bool m_Root;

public:
	CGenericTreeElementInfo(int Depth, int Root) :
		m_Depth(Depth), m_Root((Root)) {}

	int GetDepth() const { return m_Depth; }
	bool IsRoot() const { return m_Root; }
};

template<typename T>
class CGenericTreeElement
{
	T m_Value;
	std::list<CGenericTreeElement<T>> m_Children;

public:
	CGenericTreeElement() = default;
	CGenericTreeElement(T Value) :
		m_Value(Value) {}
	CGenericTreeElement(T Value, std::list<CGenericTreeElement<T>> List) :
		m_Value(Value), m_Children(List) {}

	void AddChild(CGenericTreeElement<T> Value) { m_Children.push_back(Value); }
	void SetValue(T Value) { m_Value = Value; }
	T *GetValue() { return &m_Value; }

	void Clear()
	{
		m_Value = {};
		m_Children = {};
	}

	std::list<CGenericTreeElement<T>> *GetChildren() { return &m_Children; }

	// rewrite it using no recursion with help of std::list
	template<typename F>
	T *FindElement(F Predicate)
	{
		if(Predicate(m_Value))
			return &m_Value;

		if(!m_Children.empty())
		{
			T *Found;

			for(CGenericTreeElement<T> &Child : m_Children)
			{
				Found = Child.FindElement(Predicate);

				if(Found)
					return Found;
			}
		}

		return nullptr;
	}

	std::list<std::pair<CGenericTreeElementInfo, T *>> Flatten(int Depth = 0)
	{
		std::list<std::pair<CGenericTreeElementInfo, T *>> Flattened{{{Depth, !m_Children.empty()}, &m_Value}};

		if(!m_Children.empty())
			for(CGenericTreeElement<T> &Child : m_Children)
				Flattened.splice(Flattened.end(), Child.Flatten(Depth + 1));

		return Flattened;
	}
};

typedef void (*FGameInterfaceCallback)(int, IGameInterfaceRenderer *, void *);

enum
{
	GAMEINT_ALIGN_LEFT,
	GAMEINT_ALIGN_CENTER,
	GAMEINT_ALIGN_RIGHT
};

enum
{
	GAMEINT_MAX_STRING_SIZE = 128
};

class CGameInterfaceObject
{
	char m_aString[GAMEINT_MAX_STRING_SIZE] = {0};
	int m_Alignment = GAMEINT_ALIGN_LEFT;

	FGameInterfaceCallback m_Callback = nullptr;

public:
	CGameInterfaceObject() = default;

	template<typename... Args>
	CGameInterfaceObject(const char *pFormat, Args &... vArgs)
	{
		std::snprintf(m_aString, sizeof(m_aString), pFormat, vArgs...);
	}

	template<typename... Args>
	CGameInterfaceObject(int Alignment, const char *pFormat, Args &... vArgs) :
		m_Alignment(Alignment)
	{
		std::snprintf(m_aString, sizeof(m_aString), pFormat, vArgs...);
	}

	template<typename... Args>
	CGameInterfaceObject(FGameInterfaceCallback pCallback, const char *pFormat, Args &... vArgs) :
		m_Callback(pCallback)
	{
		std::snprintf(m_aString, sizeof(m_aString), pFormat, vArgs...);
	}

	template<typename... Args>
	CGameInterfaceObject(int Alignment, FGameInterfaceCallback pCallback, const char *pFormat, Args &... vArgs) :
		m_Alignment(Alignment), m_Callback(pCallback)
	{
		std::snprintf(m_aString, sizeof(m_aString), pFormat, vArgs...);
	}

	void SetAlignment(int Alignment) { m_Alignment = Alignment; }
	void SetCallback(FGameInterfaceCallback pCallback) { m_Callback = pCallback; }

	int GetAlignment() const { return m_Alignment; }
	const char *GetText() const { return m_aString; }
	FGameInterfaceCallback GetCallback() const { return m_Callback; }
};

#endif
