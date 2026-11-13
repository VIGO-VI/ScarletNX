/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* A storage image the texture cache makes no surface for: a header with a format and no address, which a game leaves
   bound where a pass doesn't store. The dispatch is skipped and counted as a storage image not bound, as such a header
   always was, and nothing reads the surface it has none of (a dispatch read its address first, and crashed) */
static int check_storage_image_refused(void) {
    const u64 address = 0x751000000ull, program = address + 0x1000, cbuf = address + 0x2000,
              descriptors = address + 0x3000;
    if (!g_vk.feat.shaderStorageImageWriteWithoutFormat) {
        puts("storage image refused: storage writes without a format are unavailable, skipped");
        return 0;
    }
    /* SUST.P [R0], R8 through the bound handle at c[2][0x12C]: header 0 */
    const u64 store = (0xeb20000000070008ull | 3ull << 33 | 1ull << 20 | 1ull << 51) | 75ull << 36;
    const u64 instructions[] = {storage_mov(0, 0), storage_mov(1, 0), storage_mov(8, 0x3f800000), store,
                                0xe30000000007000full};
    /* R32F, 4x4, block linear of one GOB, and no address */
    const u32 word0 = 0x0f | 7u << 7 | 7u << 10 | 7u << 13 | 7u << 16 | 2u << 19 | 3u << 22 | 4u << 25 | 5u << 28;
    const u32 tic[8] = {word0, 0, 3u << 21, 0, 3u | 1u << 23, 3u | 1u << 31, 0, 0};
    u8 code[256] = {0};
    u32 at = 0, qmd[64] = {0};
    char why[256];
    for (u32 i = 0; i < sizeof instructions / sizeof instructions[0]; i++) {
        if (!(i % 3)) snx_st_le64(code + 8 * at++, 0x001f8000fc0007e0ull);
        snx_st_le64(code + 8 * at++, instructions[i]);
    }
    GpuChannel* channel = calloc(1, sizeof *channel);
    CHECK(channel);
    CHECK(!mem_map(HEAP_BASE + 0x11E40000, 0x10000, MS_NORMAL, PERM_RW));
    gmmu_map(address, HEAP_BASE + 0x11E40000, 0x10000);
    channel->rcomp[KEPLER_COMPUTE_TIC] = (u32)(descriptors >> 32);
    channel->rcomp[KEPLER_COMPUTE_TIC + 1] = (u32)descriptors;
    channel->rcomp[KEPLER_COMPUTE_TEX_CB_INDEX] = 2;
    CHECK(!storage_write_words(cbuf + 0x12C, (const u32[1]){0}, 1));
    CHECK(!storage_write_words(descriptors, tic, 8));
    CHECK(!gmem_write(program, code, sizeof code));
    qmd[QMD_GRID_DIM_X] = 1;
    qmd[QMD_GRID_DIM_YZ] = 1 | 1u << 16;
    qmd[QMD_BLOCK_DIM_X] = 1u << 16;
    qmd[QMD_BLOCK_DIM_YZ] = 1 | 1u << 16;
    qmd[QMD_CONST_BUFFER_ENABLE] = 1u << 2;
    qmd[QMD_CONST_BUFFER_CONFIG + 4] = (u32)cbuf;
    qmd[QMD_CONST_BUFFER_CONFIG + 5] = (u32)(cbuf >> 32) | 0x200u << 15;
    u64 skipped = vk_cs_skipped(why, sizeof why);
    vk_lock();
    {
        DispatchState d;
        mx_dispatch_state(qmd, channel->rcomp, program, &d);
        vk_dispatch(&d);
    }
    vk_unlock();
    u64 after = vk_cs_skipped(why, sizeof why);
    free(channel);
    CHECK(!mem_unmap(HEAP_BASE + 0x11E40000, 0x10000));
    if (after != skipped + 1 || !strstr(why, "a storage image not bound")) {
        fprintf(stderr, "storage image refused: %llu dispatches skipped, not %llu, as%s\n", (unsigned long long)after,
                (unsigned long long)(skipped + 1), why);
        return 1;
    }
    puts("storage image refused: a header the texture cache makes no surface for skips its dispatch");
    return 0;
}
