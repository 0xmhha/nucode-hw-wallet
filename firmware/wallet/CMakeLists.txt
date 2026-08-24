cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nuwallet)

# 암호 스택은 Arduino 스케치(../nuwallet)와 공유한다. 한 곳에만 둔다.
set(SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../nuwallet/src)

target_sources(app PRIVATE
    src/main.c


    # 코어는 Arduino 스케치와 공유한다 (Arduino 는 스케치 폴더 밖을 컴파일하지
    # 못해서 코어가 그쪽에 있어야 한다). 두 포트가 같은 파일을 컴파일한다.
    ${SHARED}/core/framing.c
    ${SHARED}/core/rlp.c
    ${SHARED}/core/store.c
    ${SHARED}/core/wire.c
    ${SHARED}/core/session.c
    ${SHARED}/core/challenge.c
    ${SHARED}/core/commands.c
    ${SHARED}/core/wallet.c

    src/port/zephyr/hal_zephyr.c
    src/port/zephyr/ble.c

    ${SHARED}/crypto/sha2.c
    ${SHARED}/crypto/hmac.c
    ${SHARED}/crypto/keccak.c
    ${SHARED}/crypto/bip39.c
    ${SHARED}/crypto/bip32.c
    ${SHARED}/crypto/slip10.c
    ${SHARED}/crypto/ecdsa.c

    ${SHARED}/micro-ecc/uECC.c
)

target_include_directories(app PRIVATE src ${SHARED})

# micro-ecc 는 플랫폼 매크로가 없으면 인라인 어셈블리를 고른다.
# Cortex-M4 에서는 그쪽이 빠르지만, 빌드가 깨지면 아래를 켜서 C 로 떨어뜨린다.
# target_compile_definitions(app PRIVATE uECC_PLATFORM=uECC_arch_other)
