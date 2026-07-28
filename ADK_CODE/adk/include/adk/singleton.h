/**
 * @file singleton.h
 * @brief 单例模式实现
 * @author Li Yunchong
 * @version 0.1
 * @date 2017-01-16
 */
#ifndef ADK_IMPL_SINGLETON_H_
#define ADK_IMPL_SINGLETON_H_

#include <map>
#include <mutex>
#include <tuple>
#include <type_traits> //remove_reference

namespace adk_impl
{

/**
 * @brief 单例模式实现类模板
 *
 * 本实现是对基础单例模式实现的扩展，支持使用不同的构造参数创建某个类的的不同单例。
 *
 * @tparam T 实现单例模式的基础类型
 */
template <typename T>
class Singleton
{
public:
    /**
     * @brief 获取无构造参数类的单例对象指针
     *
     * @return T类的单例对象指针
     */
    static inline T* GetInstance();

    /**
     * @brief 获取有对应一组构造参数的单例对象指针
     *
     * 例如，类C的构造函数形式为: C:C(int a, double b)，则：
     * 每次调用GetInstance<C>(0, 0.1)会返回同一个C类的指针；
     * 调用GetInstanc<C>(0, 0.1), GetInstance<C>(0, 0.2), GetInstance<C>(1, 0.1)将分别返回三个不同的指针。
     *
     * @tparam Types    构造参数类型列表
     * @param args      构造参数列表
     *
     * @return T类以args为构造参数的单例对象指针
     */
    template <typename... Types>
    static T* GetInstance(Types&&... args);
};

template <typename T>
T* Singleton<T>::GetInstance()
{
    static T t;
    return &t;
}

template <typename T>
template <typename... Types>
T* Singleton<T>::GetInstance(Types&&... args)
{
    typedef std::tuple
        <typename std::remove_reference<Types>::type...> TupleType;
    
    static std::map<TupleType, T*> ins_map;
    static std::mutex mtx;
    const auto& key = std::make_tuple(std::forward<Types>(args)...);
    std::unique_lock<std::mutex> lock(mtx);
    auto it = ins_map.find(key);
    if (it != ins_map.end())
    { return it->second; }
    
    try
    {
        T* new_t  = new T(std::forward<Types>(args)...);
        ins_map[key] = new_t;
        return new_t;
    }
    catch (...)
    {
        return NULL;
    }
}

}

#endif /* AMI_SINGLETON_H_ */
