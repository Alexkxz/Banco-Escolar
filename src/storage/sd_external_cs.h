#pragma once

// Selection is deliberately abstracted from Arduino GPIO so an external
// controller such as the board's CH422G can own the physical CS line.
class SdExternalChipSelect {
public:
    using WriteCallback = bool (*)(void *context, bool selected);

    bool configure(WriteCallback callback, void *context)
    {
        callback_ = callback;
        context_ = context;
        selected_ = false;
        return deselect();
    }

    bool select()
    {
        if (callback_ == nullptr || !callback_(context_, true)) {
            // A failed assertion may have reached the device, so always try
            // to leave the physical line inactive before returning.
            if (callback_ != nullptr && callback_(context_, false)) selected_ = false;
            return false;
        }
        selected_ = true;
        return true;
    }

    bool deselect()
    {
        if (callback_ == nullptr || !callback_(context_, false)) return false;
        selected_ = false;
        return true;
    }

    bool isSelected() const { return selected_; }

private:
    WriteCallback callback_ = nullptr;
    void *context_ = nullptr;
    bool selected_ = false;
};
