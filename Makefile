all: nfsping

CFLAGS=-Werror -g -I src

nfsping: src/nfsping.c src/nfsping.h src/nfs_prot_clnt.c src/mount_clnt.c src/util.c
	gcc ${CFLAGS} src/nfsping.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/mount_clnt.c src/mount_xdr.c src/util.c -o $@

nfs_prot.h nfs_prot_clnt.c nfs_prot_svc.c nfs_prot_xdr.c: nfs_prot.x
	rpcgen -DWANT_NFS3 $<

mount.h mount_clnt.c mount_svc.c mount_xdr.c: mount.x
	rpcgen -DWANT_NFS3 $<

nfsmount: src/mount.c src/nfsping.h src/mount_clnt.c src/mount_xdr.c
	gcc ${CFLAGS} $^ -o $@

nfsdf: src/df.c src/nfsping.h src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/util.c 
	gcc ${CFLAGS} $^ -o $@

nfsreaddir: src/readdir.c src/nfs_prot_clnt.c src/nfs_prot_xdr.c src/util.c 
	gcc ${CFLAGS} $^ -o $@

tests: tests/util_tests

tests/util_tests: tests/util_tests.c tests/minunit.h src/util.c src/util.h
	gcc ${CFLAGS} tests/util_tests.c src/util.c -o $@
