/*
 * Copyright 2015-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * blk.cpp -- pmemblk benchmarks definitions
 */

#include "benchmark.hpp"
#include "libpmemblk.h"
#include "os.h"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

struct blk_bench;
struct blk_worker;

/*
 * typedef for the worker function
 */
typedef int (*worker_fn)(struct blk_bench *, struct benchmark_args *,
			 struct blk_worker *, off_t);

/*
 * blk_args -- benchmark specific arguments
 */
struct blk_args {
	bool file_io;   /* use file-io */
	size_t fsize;   /* file size */
	bool no_warmup; /* don't do warmup */
	unsigned seed;  /* seed for randomization */
	bool rand;      /* random blocks */
};

/*
 * blk_bench -- pmemblk benchmark context
 */
struct blk_bench {
	PMEMblkpool *pbp;	 /* pmemblk handle */
	int fd;			  /* file descr. for file io */
	size_t nblocks;		  /* number of blocks */
	size_t blocks_per_thread; /* number of blocks per thread */
	worker_fn worker;	 /* worker function */
};

/*
 * struct blk_worker -- pmemblk worker context
 */
struct blk_worker {
	off_t *blocks;       /* array with block numbers */
	unsigned char *buff; /* buffer for read/write */
	unsigned seed;       /* worker seed */
};

/*
 * blk_do_warmup -- perform warm-up by writing to each block
 */
static int
blk_do_warmup(struct blk_bench *bb, struct benchmark_args *args)
{
	struct blk_args *ba = (struct blk_args *)args->opts;
	size_t lba;
	int ret = 0;
	char *buff = (char *)calloc(1, args->dsize);
	if (!buff) {
		perror("calloc");
		return -1;
	}

	for (lba = 0; lba < bb->nblocks; ++lba) {
		if (ba->file_io) {
			size_t off = lba * args->dsize;
			if (pwrite(bb->fd, buff, args->dsize, off) !=
			    (ssize_t)args->dsize) {
				perror("pwrite");
				ret = -1;
				goto out;
			}
		} else {
			if (pmemblk_write(bb->pbp, buff, lba) < 0) {
				perror("pmemblk_write");
				ret = -1;
				goto out;
			}
		}
	}

out:
	free(buff);
	return ret;
}

/*
 * blk_read -- read function for pmemblk
 */
static int
blk_read(struct blk_bench *bb, struct benchmark_args *ba,
	 struct blk_worker *bworker, off_t off)
{
	if (pmemblk_read(bb->pbp, bworker->buff, off) < 0) {
		perror("pmemblk_read");
		return -1;
	}
	return 0;
}

/*
 * fileio_read -- read function for file io
 */
static int
fileio_read(struct blk_bench *bb, struct benchmark_args *ba,
	    struct blk_worker *bworker, off_t off)
{
	off_t file_off = off * ba->dsize;
	if (pread(bb->fd, bworker->buff, ba->dsize, file_off) !=
	    (ssize_t)ba->dsize) {
		perror("pread");
		return -1;
	}
	return 0;
}

/*
 * blk_write -- write function for pmemblk
 */
static int
blk_write(struct blk_bench *bb, struct benchmark_args *ba,
	  struct blk_worker *bworker, off_t off)
{
	if (pmemblk_write(bb->pbp, bworker->buff, off) < 0) {
		perror("pmemblk_write");
		return -1;
	}
	return 0;
}

/*
 * fileio_write -- write function for file io
 */
static int
fileio_write(struct blk_bench *bb, struct benchmark_args *ba,
	     struct blk_worker *bworker, off_t off)
{
	off_t file_off = off * ba->dsize;
	if (pwrite(bb->fd, bworker->buff, ba->dsize, file_off) !=
	    (ssize_t)ba->dsize) {
		perror("pwrite");
		return -1;
	}
	return 0;
}

/*
 * blk_operation -- main operations for blk_read and blk_write benchmark
 */
static int
blk_operation(struct benchmark *bench, struct operation_info *info)
{
	struct blk_bench *bb = (struct blk_bench *)pmembench_get_priv(bench);
	struct blk_worker *bworker = (struct blk_worker *)info->worker->priv;

	off_t off = bworker->blocks[info->index];
	return bb->worker(bb, info->args, bworker, off);
}

/*
 * blk_init_worker -- initialize worker
 */
static int
blk_init_worker(struct benchmark *bench, struct benchmark_args *args,
		struct worker_info *worker)
{
	struct blk_worker *bworker =
		(struct blk_worker *)malloc(sizeof(*bworker));

	if (!bworker) {
		perror("malloc");
		return -1;
	}

	struct blk_bench *bb = (struct blk_bench *)pmembench_get_priv(bench);
	struct blk_args *bargs = (struct blk_args *)args->opts;

	bworker->seed = os_rand_r(&bargs->seed);

	bworker->buff = (unsigned char *)malloc(args->dsize);
	if (!bworker->buff) {
		perror("malloc");
		goto err_buff;
	}

	/* fill buffer with some random data */
	memset(bworker->buff, bworker->seed, args->dsize);

	assert(args->n_ops_per_thread != 0);
	bworker->blocks = (off_t *)malloc(sizeof(*bworker->blocks) *
					  args->n_ops_per_thread);
	if (!bworker->blocks) {
		perror("malloc");
		goto err_blocks;
	}

	if (bargs->rand) {
		for (size_t i = 0; i < args->n_ops_per_thread; i++) {
			bworker->blocks[i] =
				worker->index * bb->blocks_per_thread +
				os_rand_r(&bworker->seed) %
					bb->blocks_per_thread;
		}
	} else {
		for (size_t i = 0; i < args->n_ops_per_thread; i++)
			bworker->blocks[i] = i % bb->blocks_per_thread;
	}

	worker->priv = bworker;
	return 0;
err_blocks:
	free(bworker->buff);
err_buff:
	free(bworker);

	return -1;
}

/*
 * blk_free_worker -- cleanup worker
 */
static void
blk_free_worker(struct benchmark *bench, struct benchmark_args *args,
		struct worker_info *worker)
{
	struct blk_worker *bworker = (struct blk_worker *)worker->priv;
	free(bworker->blocks);
	free(bworker->buff);
	free(bworker);
}

/*
 * blk_init -- function for initialization benchmark
 */
static int
blk_init(struct blk_bench *bb, struct benchmark_args *args)
{
	struct blk_args *ba = (struct blk_args *)args->opts;
	assert(ba != NULL);

	if (ba->fsize == 0)
		ba->fsize = PMEMBLK_MIN_POOL;

	if (ba->fsize / args->dsize < args->n_threads ||
	    ba->fsize < PMEMBLK_MIN_POOL) {
		fprintf(stderr, "too small file size\n");
		return -1;
	}

	if (args->dsize >= ba->fsize) {
		fprintf(stderr, "block size bigger than file size\n");
		return -1;
	}

	if (args->is_poolset) {
		if (args->fsize < ba->fsize) {
			fprintf(stderr, "insufficient size of poolset\n");
			return -1;
		}

		ba->fsize = 0;
	}

	bb->fd = -1;
	/*
	 * Create pmemblk in order to get the number of blocks
	 * even for file-io mode.
	 */
	bb->pbp = pmemblk_create(args->fname, args->dsize, ba->fsize,
				 args->fmode);
	if (bb->pbp == NULL) {
		perror("pmemblk_create");
		return -1;
	}

	bb->nblocks = pmemblk_nblock(bb->pbp);

	if (bb->nblocks < args->n_threads) {
		fprintf(stderr, "too small file size");
		goto out_close;
	}

	if (ba->file_io) {
		pmemblk_close(bb->pbp);
		bb->pbp = NULL;

		int flags = O_RDWR | O_CREAT | O_SYNC;
#ifdef _WIN32
		flags |= O_BINARY;
#endif
		bb->fd = os_open(args->fname, flags, args->fmode);
		if (bb->fd < 0) {
			perror("open");
			return -1;
		}
	}

	bb->blocks_per_thread = bb->nblocks / args->n_threads;

	if (!ba->no_warmup) {
		if (blk_do_warmup(bb, args) != 0)
			goto out_close;
	}

	return 0;
out_close:
	if (ba->file_io)
		os_close(bb->fd);
	else
		pmemblk_close(bb->pbp);
	return -1;
}

/*
 * blk_read_init - function for initializing blk_read benchmark
 */
static int
blk_read_init(struct benchmark *bench, struct benchmark_args *args)
{
	assert(bench != NULL);
	assert(args != NULL);

	int ret;
	struct blk_args *ba = (struct blk_args *)args->opts;
	struct blk_bench *bb =
		(struct blk_bench *)malloc(sizeof(struct blk_bench));
	if (bb == NULL) {
		perror("malloc");
		return -1;
	}

	pmembench_set_priv(bench, bb);

	if (ba->file_io)
		bb->worker = fileio_read;
	else
		bb->worker = blk_read;

	ret = blk_init(bb, args);
	if (ret != 0)
		free(bb);

	return ret;
}

/*
 * blk_write_init - function for initializing blk_write benchmark
 */
static int
blk_write_init(struct benchmark *bench, struct benchmark_args *args)
{
	assert(bench != NULL);
	assert(args != NULL);

	int ret;
	struct blk_args *ba = (struct blk_args *)args->opts;
	struct blk_bench *bb =
		(struct blk_bench *)malloc(sizeof(struct blk_bench));
	if (bb == NULL) {
		perror("malloc");
		return -1;
	}

	pmembench_set_priv(bench, bb);

	if (ba->file_io)
		bb->worker = fileio_write;
	else
		bb->worker = blk_write;

	ret = blk_init(bb, args);

	if (ret != 0)
		free(bb);

	return ret;
}

/*
 * blk_exit -- function for de-initialization benchmark
 */
static int
blk_exit(struct benchmark *bench, struct benchmark_args *args)
{
	struct blk_bench *bb = (struct blk_bench *)pmembench_get_priv(bench);
	struct blk_args *ba = (struct blk_args *)args->opts;

	if (ba->file_io) {
		os_close(bb->fd);
	} else {
		pmemblk_close(bb->pbp);
		int result = pmemblk_check(args->fname, args->dsize);
		if (result < 0) {
			perror("pmemblk_check error");
			return -1;
		} else if (result == 0) {
			perror("pmemblk_check: not consistent");
			return -1;
		}
	}

	free(bb);
	return 0;
}

static struct benchmark_clo blk_clo[5];
static struct benchmark_info blk_read_info;
static struct benchmark_info blk_write_info;

CONSTRUCTOR(blk_costructor)
void
blk_costructor(void)
{
	blk_clo[0].opt_short = 'i';
	blk_clo[0].opt_long = "file-io";
	blk_clo[0].descr = "File I/O mode";
	blk_clo[0].type = CLO_TYPE_FLAG;
	blk_clo[0].off = clo_field_offset(struct blk_args, file_io);
	blk_clo[0].def = "false";

	blk_clo[1].opt_short = 'w';
	blk_clo[1].opt_long = "no-warmup";
	blk_clo[1].descr = "Don't do warmup";
	blk_clo[1].type = CLO_TYPE_FLAG;
	blk_clo[1].off = clo_field_offset(struct blk_args, no_warmup);

	blk_clo[2].opt_short = 'r';
	blk_clo[2].opt_long = "random";
	blk_clo[2].descr = "Use random block numbers "
			   "for write/read";
	blk_clo[2].off = clo_field_offset(struct blk_args, rand);
	blk_clo[2].type = CLO_TYPE_FLAG;

	blk_clo[3].opt_short = 'S';
	blk_clo[3].opt_long = "seed";
	blk_clo[3].descr = "Random mode";
	blk_clo[3].off = clo_field_offset(struct blk_args, seed);
	blk_clo[3].def = "1";
	blk_clo[3].type = CLO_TYPE_UINT;
	blk_clo[3].type_uint.size = clo_field_size(struct blk_args, seed);
	blk_clo[3].type_uint.base = CLO_INT_BASE_DEC;
	blk_clo[3].type_uint.min = 1;
	blk_clo[3].type_uint.max = UINT_MAX;

	blk_clo[4].opt_short = 's';
	blk_clo[4].opt_long = "file-size";
	blk_clo[4].descr = "File size in bytes - 0 means "
			   "minimum";
	blk_clo[4].type = CLO_TYPE_UINT;
	blk_clo[4].off = clo_field_offset(struct blk_args, fsize);
	blk_clo[4].def = "0";
	blk_clo[4].type_uint.size = clo_field_size(struct blk_args, fsize);
	blk_clo[4].type_uint.base = CLO_INT_BASE_DEC;
	blk_clo[4].type_uint.min = 0;
	blk_clo[4].type_uint.max = ~0;

	blk_read_info.name = "blk_read";
	blk_read_info.brief = "Benchmark for blk_read() operation";
	blk_read_info.init = blk_read_init;
	blk_read_info.exit = blk_exit;
	blk_read_info.multithread = true;
	blk_read_info.multiops = true;
	blk_read_info.init_worker = blk_init_worker;
	blk_read_info.free_worker = blk_free_worker;
	blk_read_info.operation = blk_operation;
	blk_read_info.clos = blk_clo;
	blk_read_info.nclos = ARRAY_SIZE(blk_clo);
	blk_read_info.opts_size = sizeof(struct blk_args);
	blk_read_info.rm_file = true;
	blk_read_info.allow_poolset = true;

	REGISTER_BENCHMARK(blk_read_info);

	blk_write_info.name = "blk_write";
	blk_write_info.brief = "Benchmark for blk_write() operation";
	blk_write_info.init = blk_write_init;
	blk_write_info.exit = blk_exit;
	blk_write_info.multithread = true;
	blk_write_info.multiops = true;
	blk_write_info.init_worker = blk_init_worker;
	blk_write_info.free_worker = blk_free_worker;
	blk_write_info.operation = blk_operation;
	blk_write_info.clos = blk_clo;
	blk_write_info.nclos = ARRAY_SIZE(blk_clo);
	blk_write_info.opts_size = sizeof(struct blk_args);
	blk_write_info.rm_file = true;
	blk_write_info.allow_poolset = true;

	REGISTER_BENCHMARK(blk_write_info);
}
