.DELETE_ON_ERROR:

-include $(patsubst %.cpp, .build/%.d, $(wildcard *.cpp))

V = 0
AT_0 := @
AT_1 :=
AT = $(AT_$(V))

ifeq ($(V), 1)
    PRINTF := @\#
else
    PRINTF := @printf
endif

CXX=clang++
CXXFLAGS=-O3 -MMD -MP -std=c++14 -g -Wno-documentation-deprecated-sync \
         -Wno-documentation -Wno-padded -Wno-unused-const-variable \
         -Wno-reserved-id-macro -Wno-c99-extensions

ifeq ($(CXX), clang++)
    CXXFLAGS+=-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic \
        -Wno-poison-system-directories
endif

LDFLAGS=-ldl -g
LD=$(CXX)

sync_history: .build/sync_history.o
	$(PRINTF) " LD\t$@\n"
	$(AT)$(LD) $(LDFLAGS) $^ -o $@

.build/:
	$(AT)mkdir -p "$@"

.build/%.o: %.cpp | .build/
	$(PRINTF) " CXX\t$*.cpp\n"
	$(AT)$(CXX) $(CXXFLAGS) -I. $< -c -o $@

.PHONY: clean
clean:
	$(PRINTF) " cleaning\n"
	$(AT)rm sync_history.o sync_history.d
