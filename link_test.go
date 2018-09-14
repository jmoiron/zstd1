package zstd1

import (
	"testing"

	"github.com/DataDog/zstd"
)

func TestMultiLink(t *testing.T) {
	_ = zstd.Decompress
}
