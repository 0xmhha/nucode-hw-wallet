# 호스트에서 도는 테스트. 보드도 Zephyr SDK 도 필요 없다.
#
#   make test       암호 스택 + 지갑 코어 전부
#   make test_wallet && ./test_wallet
CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Wno-unused-parameter \
          -DuECC_PLATFORM=uECC_arch_other -I../nuwallet/src

SHARED = ../nuwallet/src
ZSRC   = ../zephyr/src

# 파일을 나열하지 않고 훑는다 — 암호 스택은 계속 늘어나는 중이라
# 하나 추가할 때마다 Makefile 을 고치게 하지 않는다.
CRYPTO  = $(wildcard $(SHARED)/crypto/*.c) $(SHARED)/micro-ecc/uECC.c
CHAINS  = $(wildcard $(SHARED)/chains/ethereum/*.c)
APP     = $(wildcard $(ZSRC)/app/*.c) $(ZSRC)/port/host/hal_host.c

.PHONY: all test clean
all: test_crypto test_wallet protocol_bridge

test_crypto: test_crypto.c vectors_bip39.h vectors_bip32.h vectors_ecdsa.h $(CRYPTO) $(CHAINS)
	$(CC) $(CFLAGS) -o $@ test_crypto.c $(CRYPTO) $(CHAINS)

test_wallet: test_wallet.c $(CRYPTO) $(APP)
	$(CC) $(CFLAGS) -o $@ test_wallet.c $(CRYPTO) $(APP)

# SDK 적합성 테스트(sdk/test/conformance.test.js)가 자식 프로세스로 띄운다.
protocol_bridge: protocol_bridge.c $(CRYPTO) $(APP)
	$(CC) $(CFLAGS) -o $@ protocol_bridge.c $(CRYPTO) $(APP)

test: test_crypto test_wallet
	./test_crypto
	./test_wallet

clean:
	rm -f test_crypto test_wallet protocol_bridge
