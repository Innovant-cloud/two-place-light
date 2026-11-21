// tests/test_prov_parse.c —— 配网表单解析的宿主机测试。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "prov_parse.h"

static prov_result_t parse(const char *body, char *ssid, size_t ssid_cap, char *pass,
                           size_t pass_cap) {
    return prov_parse_form(body, strlen(body), ssid, ssid_cap, pass, pass_cap);
}

int main(void) {
    char ssid[40];
    char pass[72];

    // 1. 基本形式。
    assert(parse("ssid=Office&pass=12345678", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "Office") == 0);
    assert(strcmp(pass, "12345678") == 0);

    // 2. %XX 还原与 '+' 还原成空格。
    assert(parse("ssid=My%20Home&pass=a%2Bb%3DcDEFGH", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "My Home") == 0);
    assert(strcmp(pass, "a+b=cDEFGH") == 0);
    assert(parse("ssid=Cafe+Net&pass=correct+horse", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "Cafe Net") == 0);
    assert(strcmp(pass, "correct horse") == 0);

    // 3. 字段顺序无关，多余字段忽略。
    assert(parse("pass=12345678&ssid=Lab", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "Lab") == 0);
    assert(parse("x=1&ssid=Lab&pass=12345678&z=2", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "Lab") == 0);

    // 4. 开放网络：密码为空是合法的。
    assert(parse("ssid=FreeWiFi&pass=", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(ssid, "FreeWiFi") == 0);
    assert(strcmp(pass, "") == 0);
    assert(parse("ssid=FreeWiFi", ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
    assert(strcmp(pass, "") == 0);

    // 5. 各类拒绝。
    assert(parse("hello", ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_FORMAT);
    assert(parse("pass=12345678", ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_FORMAT);
    assert(parse("", ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_FORMAT);
    assert(parse("ssid=&pass=12345678", ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_SSID);
    assert(parse("ssid=012345678901234567890123456789012&pass=12345678", ssid, sizeof ssid, pass,
                 sizeof pass) == PROV_ERR_SSID); // 33 字节
    assert(parse("ssid=Ok&pass=1234567", ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_PASS);
    assert(parse("ssid=Ok&pass=0123456789012345678901234567890123456789012345678901234567890123",
                 ssid, sizeof ssid, pass, sizeof pass) == PROV_ERR_PASS); // 64 字节

    // 6. 恰好 32 字节的 SSID 与 63 字节的密码是合法的。
    {
        const char *ssid32 = "01234567890123456789012345678901"; // 32
        const char *pass63 = "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabc";
        char body[160];
        (void)snprintf(body, sizeof body, "ssid=%s&pass=%s", ssid32, pass63);
        assert(parse(body, ssid, sizeof ssid, pass, sizeof pass) == PROV_OK);
        assert(strlen(ssid) == 32);
        assert(strlen(pass) == 63);
    }

    // 7. 输出缓冲区太小 -> BUFFER，且不留半截字符串。
    {
        char small[4];
        assert(parse("ssid=Office&pass=12345678", small, sizeof small, pass, sizeof pass) ==
               PROV_ERR_BUFFER);
        assert(small[0] == '\0');
    }

    // 8. 失败时不把上一次的残值留在缓冲区里。
    {
        char s2[40] = "stale";
        char p2[72] = "stale";
        assert(parse("ssid=&pass=12345678", s2, sizeof s2, p2, sizeof p2) == PROV_ERR_SSID);
        assert(s2[0] == '\0');
        assert(p2[0] == '\0');
    }

    // 9. 单独的 url_decode 行为。
    {
        char out[16];
        assert(prov_url_decode("a+b", out, sizeof out) == 3);
        assert(strcmp(out, "a b") == 0);
        assert(prov_url_decode("%41%42", out, sizeof out) == 2);
        assert(strcmp(out, "AB") == 0);
        // 非法转义按字面保留，不吞字符。
        assert(prov_url_decode("%zz", out, sizeof out) == 3);
        assert(strcmp(out, "%zz") == 0);
        // 截断但不越界。
        char tiny[3];
        assert(prov_url_decode("abcdef", tiny, sizeof tiny) == 2);
        assert(strcmp(tiny, "ab") == 0);
    }

    (void)printf("test_prov_parse: PASS\n");
    return 0;
}
