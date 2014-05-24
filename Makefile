.PHONY: all clean nfsping nfsmount nfsdf nfscat
all: nfsping nfsmount nfsdf nfsls nfscat

clean:
	rm -rf obj bin

#output directories
bin:
	mkdir $@

obj:
	mkdir $@

CFLAGS=-Werror -g -I src

# build RPC headers first
src/nfsping.h: src/nfs_prot.h src/mount.h

#rpcgen NFS
src/nfs_prot.h src/nfs_prot_clnt.c src/nfs_prot_svc.c src/nfs_prot_xdr.c: src/nfs_prot.x
	cd src && rpcgen -DWANT_NFS3 nfs_prot.x

#rpcgen MOUNT
src/mount.h src/mount_clnt.c src/mount_svc.c src/mount_xdr.c: src/mount.x
	cd src && rpcgen -DWANT_NFS3 mount.x

#objects
obj/%.o: src/%.c | obj
	gcc ${CFLAGS} -c -o $@ $<

obj/util.o: src/util.c src/util.h | obj
	gcc ${CFLAGS} -c -o $@ $<

obj/nfsping.o: src/nfsping.c src/nfsping.h | obj
	gcc ${CFLAGS} -c -o $@ $<

nfsping: bin/nfsping
bin/nfsping: $(addprefix obj/, nfsping.o nfs_prot_clnt.o nfs_prot_xdr.o mount_clnt.o mount_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsmount: bin/nfsmount
bin/nfsmount: $(addprefix obj/, mount.o mount_clnt.o mount_xdr.o rpc.o util.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsdf: bin/nfsdf
bin/nfsdf: $(addprefix obj/, df.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfsls: bin/nfsls
bin/nfsls: $(addprefix obj/, ls.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

nfscat: bin/nfscat
bin/nfscat: $(addprefix obj/, cat.o nfs_prot_clnt.o nfs_prot_xdr.o util.o rpc.o) | bin
	gcc ${CFLAGS} $^ -o $@

tests: tests/util_tests
tests/util_tests: tests/util_tests.c tests/minunit.h util.o util.h
	gcc ${CFLAGS} $^ -o $@
