Import("env")

# Add bz2 library to linker flags for bz2 compression support
env.Append(LIBS=["bz2"])
