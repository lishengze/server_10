#include <adk/util.h>

#include <iostream>

bool flag = false;
void func()
{
	std::cout << "hello";
	flag = true;
}

int main(int argc, char const *argv[])
{

	ADK_CONDITIONAL_CALL_ONCE(flag == false, func);
	ADK_CONDITIONAL_CALL_ONCE(flag == false, func);

	bool flag2 = true;
	adk::conditional_call_once(flag2, [&]{std::cout << " world!" << std::endl; flag2 = false;});
	adk::conditional_call_once(flag2, [&]{std::cout << " world!" << std::endl; flag2 = false;});

	return 0;
}