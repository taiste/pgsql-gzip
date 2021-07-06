# Make sure we do not run any code when using deb-* target
ifeq (,$(findstring deb-,$(MAKECMDGOALS)))


ZLIB_INC = /usr/local/Cellar/libdeflate/1.7/include
ZLIB_LIB = -ldeflate
ZLIB_LIBDIR = -L/usr/local/Cellar/libdeflate/1.7/lib
DEBUG = 0

# These should not require modification
MODULE_big = gzip
OBJS = pg_gzip.o
EXTENSION = gzip
DATA = gzip--1.0.sql
REGRESS = gzip
EXTRA_CLEAN =

PG_CONFIG = pg_config

CFLAGS += $(ZLIB_INC)
LDFLAGS += $(ZLIB_LIBDIR)
LIBS += $(ZLIB_LIB)
SHLIB_LINK := $(LIBS)

ifdef DEBUG
COPT += -O0 -g
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

endif


.PHONY: deb
deb: clean
	pg_buildext updatecontrol
	dpkg-buildpackage -B

# Name of the base Docker image to use. Uses debian:sid by default
base ?= debian:sid

.PHONY: deb-docker
deb-docker:
	@echo "*** Using base=$(base)"
	docker build "--build-arg=BASE_IMAGE=$(base)" -t pgsql-gzip-$(base) .
	# Create a temp dir that we will remove later. Otherwise docker will create a root-owned dir.
	mkdir -p "$$(pwd)/target/pgsql-gzip"
	docker run --rm -ti -u $$(id -u $${USER}):$$(id -g $${USER}) -v "$$(pwd)/target:/build" -v "$$(pwd):/build/pgsql-gzip" pgsql-gzip-$(base) make deb
	rmdir "$$(pwd)/target/pgsql-gzip" || true
