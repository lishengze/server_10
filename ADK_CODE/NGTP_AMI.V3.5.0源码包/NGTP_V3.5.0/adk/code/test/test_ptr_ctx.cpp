#include <adk/pointer_context.h>
#include <stdio.h>
#include <stdint.h>

using namespace adk;

int main(int argc, char const *argv[])
{
	char* ptr_c = (char*)0x1000;
	int* ptr_i = (int*)0x2000;

	printf("%p %p\n", ptr_c, ptr_i);

	printf("%lx\n", ADK_PTR_CTX_MASK);
	printf("%lx\n", ADK_PTR_VAL_MASK);

	adk::set_ptr_ctx(ptr_c, 1);
	adk::set_ptr_ctx(ptr_i, 2);
	printf("%p %p\n", ptr_c, ptr_i);

	adk::set_ptr_ctx(ptr_c, 2);
	adk::set_ptr_ctx(ptr_i, 1);
	printf("%p %p\n", ptr_c, ptr_i);

	printf("%p %p\n", adk::get_ptr_val(ptr_c), adk::get_ptr_val(ptr_i));

	char* c_ptr = adk::get_ptr_val(ptr_c);
	(void) c_ptr;

	int* i_ptr = adk::get_ptr_val(ptr_i);
	(void) i_ptr;

	uint64_t status = 4;
	adk::set_ptr_ctx(ptr_c, status);
	status = 7;
	adk::set_ptr_ctx(ptr_i, status);
	printf("%p %p\n", ptr_c, ptr_i);

	int* v_ptr1 = new int;
    (void) v_ptr1;
	int* v_ptr2 = new int;
    (void) v_ptr2;
	int* v_ptr = new int;
	printf("new %p\n", v_ptr);
	adk::set_ptr_ctx(v_ptr, status);
	printf("new %p\n", v_ptr);

	uint64_t* ptr64 = new uint64_t;
	assert(check_alignment(ptr64) == true);
	ptr64 = (uint64_t*)((char*)ptr64 + 1);
	assert(check_alignment(ptr64) == false);
	return 0;
}
