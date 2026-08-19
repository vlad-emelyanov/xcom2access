// Controlled SDK implementation — only Core utility functions.
// Does NOT include the full SdkHeaders.hpp to avoid pulling in Engine/XComGame (millions of lines).

#include "GameDefines.hpp"
#include "SDK_HEADERS/Core_structs.hpp"
#include "SDK_HEADERS/Core_classes.hpp"

#ifdef _MSC_VER
#pragma pack(push, 0x4)
#endif

// UObject::GObjObjects
class TArray<class UObject*>* UObject::GObjObjects()
{
    return reinterpret_cast<TArray<UObject*>*>(GObjects);
}

// UObject::GetName
std::string UObject::GetName()
{
    return this->Name.ToString();
}

// UObject::GetNameCPP
std::string UObject::GetNameCPP()
{
    std::string nameCPP;

    if (this->IsA<UClass>())
    {
        UClass* uClass = reinterpret_cast<UClass*>(this);

        while (uClass)
        {
            std::string className = uClass->GetName();

            if (className == "Actor")
            {
                nameCPP += "A";
                break;
            }
            else if (className == "Object")
            {
                nameCPP += "U";
                break;
            }

            uClass = reinterpret_cast<UClass*>(uClass->SuperField);
        }
    }
    else
    {
        nameCPP += "F";
    }

    nameCPP += this->GetName();

    return nameCPP;
}

// UObject::GetFullName
std::string UObject::GetFullName()
{
    std::string fullName = this->GetName();

    for (UObject* uOuter = this->Outer; uOuter; uOuter = uOuter->Outer)
    {
        fullName = (uOuter->GetName() + "." + fullName);
    }

    fullName = (this->Class->GetName() + " " + fullName);
    return fullName;
}

// UObject::GetPackageObj
class UObject* UObject::GetPackageObj()
{
    UObject* uPackage = nullptr;

    for (UObject* uOuter = this->Outer; uOuter; uOuter = uOuter->Outer)
    {
        uPackage = uOuter;
    }

    return uPackage;
}

// UObject::FindClass
class UClass* UObject::FindClass(const std::string& classFullName)
{
    static std::map<std::string, UClass*> classCache;

    if (classCache.empty())
    {
        for (int32_t i = 0; i < (UObject::GObjObjects()->size() - 1); i++)
        {
            UObject* uObject = UObject::GObjObjects()->at(i);

            if (uObject)
            {
                std::string objectFullName = uObject->GetFullName();

                if (objectFullName.find("Class") == 0)
                {
                    classCache[objectFullName] = reinterpret_cast<UClass*>(uObject);
                }
            }
        }
    }

    if (classCache.contains(classFullName))
    {
        return classCache[classFullName];
    }

    return nullptr;
}

// UObject::IsA (by UClass pointer)
bool UObject::IsA(class UClass* uClass)
{
    for (UClass* uSuperClass = reinterpret_cast<UClass*>(this->Class); uSuperClass; uSuperClass = reinterpret_cast<UClass*>(uSuperClass->SuperField))
    {
        if (uSuperClass == uClass)
        {
            return true;
        }
    }

    return false;
}

// UObject::IsA (by GObjects index)
bool UObject::IsA(int32_t objInternalInteger)
{
    UClass* uClass = reinterpret_cast<UClass*>(UObject::GObjObjects()->at(objInternalInteger)->Class);

    if (uClass)
    {
        return this->IsA(uClass);
    }

    return false;
}

// GetVirtualFunction helper
template<typename T> T GetVirtualFunction(const void* instance, size_t index)
{
    auto vtable = *static_cast<const void***>(const_cast<void*>(instance));
    return reinterpret_cast<T>(vtable[index]);
}

// UObject::ProcessEvent — calls through vtable index 72
void UObject::ProcessEvent(class UFunction* uFunction, void* uParams, void* uResult)
{
    GetVirtualFunction<void(*)(class UObject*, class UFunction*, void*)>(this, 72)(this, uFunction, uParams);
}

// UFunction::FindFunction
class UFunction* UFunction::FindFunction(const std::string& functionFullName)
{
    static std::map<std::string, UFunction*> functionCache;

    if (functionCache.empty())
    {
        for (int32_t i = 0; i < (UObject::GObjObjects()->size() - 1); i++)
        {
            UObject* uObject = UObject::GObjObjects()->at(i);

            if (uObject)
            {
                std::string objectFullName = uObject->GetFullName();

                if (objectFullName.find("Function") == 0)
                {
                    functionCache[objectFullName] = reinterpret_cast<UFunction*>(uObject);
                }
            }
        }
    }

    if (functionCache.contains(functionFullName))
    {
        return functionCache[functionFullName];
    }

    return nullptr;
}

#ifdef _MSC_VER
#pragma pack(pop)
#endif
