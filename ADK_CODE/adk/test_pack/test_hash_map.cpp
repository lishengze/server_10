#include <adk_pack/hash_map.h>
#include <adk_pack/error_code.h>

#include <string.h>

#include <iostream>

bool TestHashMap1()
{
	const char* value = "hello world";
	auto* hash_map = adk::HashMap<boost::array<char, 6>, boost::array<char, 10>, char*>::Create(1024);
	if (nullptr == hash_map)
	{
		std::cout << "create hash map failed" << std::endl;
		return false;
	}
	
	boost::array<char, 6> key1;
	boost::array<char, 10> key2;
	
	strcpy(key1.data(), "123456");
	strcpy(key2.data(), "1234567890");
	if (adk::ErrorCode::kSuccess != hash_map->Insert(key1, key2, const_cast<char*>(value)))
	{
		std::cout << "Bug on, insert hash map failed" << std::endl;
		return false;
	}
	
	char** find_value = nullptr;
	if (adk::ErrorCode::kSuccess != hash_map->Find(key1, key2, &find_value))
	{
		std::cout << "Bug on, find value failed" << std::endl;
		return false;
	}
	
	if (nullptr == find_value)
	{
		std::cout << "Bug on, value found is nullptr" << std::endl;
		return false;
	}
	
	if (*find_value != value)
	{
		std::cout << "Bug on, value in map is not equal to insert" << std::endl;
		return false;
	}
	
	strcpy(key1.data(), "234561");
	strcpy(key2.data(), "2345678901");
	if (adk::ErrorCode::kSuccess == hash_map->Find(key1, key2, &find_value))
	{
	    std::cout << "Bug on, find non insert value success" << std::endl;
		return false;
	}
	
	return true;
}

bool TestHashMap2()
{
	int64_t value = 9876543210;
	auto* hash_map = adk::HashMap<boost::array<char, 6>, boost::array<char, 10>, int64_t>::Create(1024);
	if (nullptr == hash_map)
	{
		std::cout << "create hash map failed" << std::endl;
		return false;
	}
	
	boost::array<char, 6> key1;
	boost::array<char, 10> key2;
	
	strcpy(key1.data(), "123456");
	strcpy(key2.data(), "1234567890");
	if (adk::ErrorCode::kSuccess != hash_map->Insert(key1, key2, value))
	{
		std::cout << "Bug on, insert hash map failed" << std::endl;
		return false;
	}
	
	int64_t* find_value = nullptr;
	if (adk::ErrorCode::kSuccess != hash_map->Find(key1, key2, &find_value))
	{
		std::cout << "Bug on, find value failed" << std::endl;
		return false;
	}
	
	if (nullptr == find_value)
	{
		std::cout << "Bug on, value found is nullptr" << std::endl;
		return false;
	}
	
	if (*find_value != value)
	{
		std::cout << "Bug on, value in map is not equal to insert" << std::endl;
		return false;
	}
	
	strcpy(key1.data(), "234561");
	strcpy(key2.data(), "2345678901");
	if (adk::ErrorCode::kSuccess == hash_map->Find(key1, key2, &find_value))
	{
	    std::cout << "Bug on, find non insert value success" << std::endl;
		return false;
	}
	
	return true;
}

bool TestHashMap3()
{
	uint16_t value = 65530;
	auto* hash_map = adk::HashMap<boost::array<char, 6>, uint8_t, uint16_t>::Create(1024);
	if (nullptr == hash_map)
	{
		std::cout << "create hash map failed" << std::endl;
		return false;
	}
	
	boost::array<char, 6> key1;
	strcpy(key1.data(), "123456");
	
	uint8_t key2 = 255;
	
	if (adk::ErrorCode::kSuccess != hash_map->Insert(key1, key2, value))
	{
		std::cout << "Bug on, insert hash map failed" << std::endl;
		return false;
	}
	
	uint16_t* find_value = nullptr;
	if (adk::ErrorCode::kSuccess != hash_map->Find(key1, key2, &find_value))
	{
		std::cout << "Bug on, find value failed" << std::endl;
		return false;
	}
	
	if (nullptr == find_value)
	{
		std::cout << "Bug on, value found is nullptr" << std::endl;
		return false;
	}
	
	if (*find_value != value)
	{
		std::cout << "Bug on, value in map is not equal to insert" << std::endl;
		return false;
	}
	
	key2 = 0;
	if (adk::ErrorCode::kSuccess == hash_map->Find(key1, key2, &find_value))
	{
	    std::cout << "Bug on, find non insert value success" << std::endl;
		return false;
	}
	
	return true;
}

bool TestHashMap4()
{
	uint16_t value = 65530;
	auto* hash_map = adk::HashMap<boost::array<char, 6>, uint32_t, uint16_t>::Create(1024);
	if (nullptr == hash_map)
	{
		std::cout << "create hash map failed" << std::endl;
		return false;
	}
	
	boost::array<char, 6> key1;
	strcpy(key1.data(), "123456");

	uint32_t key2 = 987654321;
	
	if (adk::ErrorCode::kSuccess != hash_map->Insert(key1, key2, value))
	{
		std::cout << "Bug on, insert hash map failed" << std::endl;
		return false;
	}
	
	uint16_t* find_value = nullptr;
	if (adk::ErrorCode::kSuccess != hash_map->Find(key1, key2, &find_value))
	{
		std::cout << "Bug on, find value failed" << std::endl;
		return false;
	}
	
	if (nullptr == find_value)
	{
		std::cout << "Bug on, value found is nullptr" << std::endl;
		return false;
	}
	
	if (*find_value != value)
	{
		std::cout << "Bug on, value in map is not equal to insert" << std::endl;
		return false;
	}
	
	key2 = 123456789;
	if (adk::ErrorCode::kSuccess == hash_map->Find(key1, key2, &find_value))
	{
	    std::cout << "Bug on, find non insert value success" << std::endl;
		return false;
	}
	
	return true;
}

int main()
{
	std::cout << "Test case1 start ..." << std::endl;
	if (TestHashMap1())
	{
		std::cout << "Test case1 pass" << std::endl;
	}
	
	std::cout << "Test case2 start ..." << std::endl;
	if (TestHashMap2())
	{
		std::cout << "Test case2 pass" << std::endl;
	}
	
	std::cout << "Test case3 start ..." << std::endl;
	if (TestHashMap3())
	{
		std::cout << "Test case3 pass" << std::endl;
	}

	std::cout << "Test case4 start ..." << std::endl;
	if (TestHashMap4())
	{
		std::cout << "Test case4 pass" << std::endl;
	}

	return 0;
}