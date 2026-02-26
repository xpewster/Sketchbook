import usb_cdc
import usb_midi

usb_midi.disable()

# usb_cdc.enable(console=False, data=True) # TODO: fallback to CDC data if wifi is not available.

# TODO: Button to trigger usb drive
# TODO: Button to trigger console output (swap displayio group to show console instead of display)
