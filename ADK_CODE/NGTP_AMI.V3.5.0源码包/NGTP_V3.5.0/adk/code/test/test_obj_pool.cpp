#include <iostream>

#include <boost/pool/pool.hpp>
#include <adk/object_pool.h>

using namespace adk;

const int DEFALUT_TEST_VALUE = -1;

class ObjectTypeTest : public IObject
{
public:
    int test_value_;
    void Reset()
    {
        test_value_ = -1;
    }
};

int main(int argc, char const *argv[])
{
    std::cout << "Test Begin..." << std::endl;

    int test_size_1 = 1024;
    int test_size_2 = 1024;

    ObjectPool<ObjectTypeTest> *ptr_objectpool_test = ObjectPool<ObjectTypeTest>::Create("ObjectPool_Test", test_size_1);
    if (NULL == ptr_objectpool_test)
    {
        std::cout << "Create ObjectPool Failed, ERROR!" << std::endl;
        return -1;
    }

    ObjectTypeTest* ap_obj_1[test_size_1];
    ObjectTypeTest* ap_obj_2[test_size_2];

    for (int idx=0; idx<test_size_1; idx++)
    {
        ap_obj_1[idx] = ptr_objectpool_test->NewObject();
        if (NULL == ap_obj_1[idx])
        {
            std::cout << "Defalut new object Failed, while objectpool is not full, ERROR!" << std::endl;
            return -1;
        }

        assert(DEFALUT_TEST_VALUE == ap_obj_1[idx]->test_value_);
        ap_obj_1[idx]->test_value_ = idx;
    }

    for (int idx=0; idx<test_size_2; idx++)
    {
        ap_obj_2[idx] = ptr_objectpool_test->NewObjectEx(false);
        assert(NULL == ap_obj_2[idx]);
        ap_obj_2[idx] = ptr_objectpool_test->NewObject();
        assert(NULL == ap_obj_2[idx]);
    }

    for (int idx=0; idx<test_size_2; idx++)
    {
        ap_obj_2[idx] = ptr_objectpool_test->NewObjectEx(true);
        assert(NULL != ap_obj_2[idx]);
        assert(DEFALUT_TEST_VALUE == ap_obj_2[idx]->test_value_);
        ap_obj_2[idx]->test_value_ = idx;
    }

    int first_free_segment = test_size_1/2;
    for (int idx=0; idx<first_free_segment; idx++)
    {
        assert(idx == ap_obj_1[idx]->test_value_);
        assert(ErrorCode::kSuccess == ap_obj_1[idx]->Delete());
        ap_obj_1[idx] = NULL;
    }

    for (int idx=0; idx<first_free_segment; idx++)
    {
        ap_obj_1[idx] = ptr_objectpool_test->NewObjectEx(false);
        assert(NULL != ap_obj_1[idx]);
        assert(DEFALUT_TEST_VALUE == ap_obj_1[idx]->test_value_);
    }

    for (int idx=0; idx<first_free_segment; idx++)
    {
        assert(NULL == ptr_objectpool_test->NewObjectEx(false));
        assert(NULL == ptr_objectpool_test->NewObject());
    }

    for (int idx=0; idx<first_free_segment; idx++)
    {
        assert(idx == ap_obj_2[idx]->test_value_);
        assert(ErrorCode::kSuccess == ap_obj_2[idx]->Delete());
        ap_obj_2[idx] = NULL;
    }

    for (int idx=0; idx<first_free_segment; idx++)
    {
        assert(NULL == ptr_objectpool_test->NewObjectEx(false));
        assert(NULL == ptr_objectpool_test->NewObject());
    }

    for (int idx=0; idx<first_free_segment; idx++)
    {
        ap_obj_2[idx] = ptr_objectpool_test->NewObjectEx(false);
        assert(NULL == ap_obj_2[idx]);
        ap_obj_2[idx] = ptr_objectpool_test->NewObjectEx(true);
        assert(NULL != ap_obj_2[idx]);
        assert(DEFALUT_TEST_VALUE == ap_obj_2[idx]->test_value_);
    }
    
    for (int idx=0; idx<test_size_1; idx++)
    {
        if (NULL != ap_obj_1[idx])
        {
            assert(ErrorCode::kSuccess  == ap_obj_1[idx]->Delete());
        }
    }

    for (int idx=0; idx<test_size_2; idx++)
    {
        if (NULL != ap_obj_2[idx])
        {
            assert(ErrorCode::kSuccess  == ap_obj_2[idx]->Delete());
        }
    }
    return 0;
}