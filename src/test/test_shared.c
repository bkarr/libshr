#include <assert.h>
#include <string.h>
#include <sys/mman.h>

#include <shared.h>


void test_explain(void) {
	char *msg;

	msg = shr_explain(SH_OK);
	assert(memcmp(msg, "success", sizeof("success")) == 0);
	msg = shr_explain(SH_ERR_NO_MATCH);
	assert(memcmp(msg, "no match found for key", sizeof("no match found for key")) == 0);
	msg = shr_explain(SH_ERR_MAX);
	assert(memcmp(msg, "invalid status code for explain", sizeof("invalid status code for explain")) == 0);
	msg = shr_explain(SH_ERR_MAX + 1);
	assert(memcmp(msg, "invalid status code for explain", sizeof("invalid status code for explain")) == 0);
	msg = shr_explain(-1);
	assert(memcmp(msg, "invalid status code for explain", sizeof("invalid status code for explain")) == 0);
}


int main(void)
{
	test_explain();

    return 0;
}
