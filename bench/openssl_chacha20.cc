#include "shared.h"
#include <openssl/evp.h>

__asm__(".text\n\t"
	".globl safe_call\n\t"
	".type safe_call, @function\n\t"
	"safe_call:\n\t"
	".cfi_startproc\n\t"
	"push %rbp\n\t"
	"push %r12\n\t"
	"push %r13\n\t"
	"push %r14\n\t"
	"push %r15\n\t"
	"mov %rdi, %rax\n\t"
	"mov %rsi, %rdi\n\t"
	"mov %rdx, %rsi\n\t"
	"mov %rcx, %rdx\n\t"
	"mov %r8,  %rcx\n\t"
	"mov %r9,  %r8\n\t"
	"call *%rax\n\t"
	"pop %r15\n\t"
	"pop %r14\n\t"
	"pop %r13\n\t"
	"pop %r12\n\t"
	"pop %rbp\n\t"
	"ret\n\t"
	".cfi_endproc");

extern "C" long safe_call(void *f, ...);

template <typename T, typename F, typename... Ts>
static T safe_call(F *f, Ts... args) {
  return (T) safe_call((void *) f, args...);
}


void openssl_chacha20(benchmark::State& state) {
  std::vector<uint8_t> msg(state.range(0));
  for (auto& c : msg)
    c = rand();
  std::vector<uint8_t> key = {
    11, 22, 33, 44, 55, 66, 77, 88, 99, 111, 122, 133, 144, 155, 166, 177,
    188, 199, 211, 222, 233, 244, 255, 0, 10, 20, 30, 40, 50, 60, 70, 80,
  };
  std::vector<uint8_t> nonce = {98, 76, 54, 32, 10, 0, 2, 4, 6, 8, 10, 12};
  std::vector<uint8_t> out(msg.size());
  for ([[maybe_unused]] auto _ : state) {
    EVP_CIPHER_CTX *ectx = safe_call<EVP_CIPHER_CTX *>(EVP_CIPHER_CTX_new);
    safe_call<void>(EVP_EncryptInit_ex2, ectx, EVP_chacha20_poly1305(), key.data(), nonce.data(), nullptr);
    int outlen;
    if (!safe_call<int>(EVP_EncryptUpdate, ectx, out.data(), &outlen, msg.data(), msg.size()))
      std::abort();
    int tmplen;
    if (!safe_call<int>(EVP_EncryptFinal_ex, ectx, out.data() + outlen, &tmplen))
      std::abort();
    safe_call<void>(EVP_CIPHER_CTX_free, ectx);
  }
}
