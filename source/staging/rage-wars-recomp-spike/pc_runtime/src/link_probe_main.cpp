int main() {
    // The linker is the probe: WHOLE_ARCHIVE forces every generated object in,
    // exposing guest-pointer and lookup-only host requirements before a game
    // entrypoint or renderer is introduced.
    return 0;
}
