#pragma once

namespace render {
    /**
     * States that a buffer might be in
     */
    enum class BufferState {
        Read,
        Write,
        TransferSource,
        TransferDestination,
    };
}
