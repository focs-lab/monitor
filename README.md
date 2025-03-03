# Monitor

The buffering implementation here is inspired by the CAB described in [Ha et al. 09] (paper is in this repo).

TODO:
- need the blocking version in the instrumentation

## Essentially

For enqueing by the program under test:

```c
void enqueue(u64 data) {
    if (buf[i] != CLEAR)
        block();
    buf[i++] = data;
    i = i & 0xffff;     // wrap around if reached the end
}

void block() {
    int poll_i = i + CHUNK_SIZE * 2;
    while (buf[poll_i] != CLEAR) {
        spin(n);
    }
}
```

For dequeing by the monitor:

```c
void main() {
    while (true) {
        int i = ((chunk_num + 2) * CHUNK_SIZE) % buffer_size;
        while (buf[i] == CLEAR)
            spin_or_sleep();
        consume_chunk(chunk_num++);
    }
}

void consume_chunk(int chunk_num) {
    for (int i = 0; i < CHUNK_SIZE; ++i) {
        int idx = chunk_num * CHUNK_SIZE + i;
        u64 data = buf[idx];
        f(data);
    }
}
```

