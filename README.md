# Nokia3310-UI

## Build firmware (Docker)
```bash
./docker/build.sh
```

## Run in Renode
```bash
renode -P 1234 renode/run.res
```

## Show LCD screen
```bash
python tools/lcd_viewer.py \
  --address 0x200002A8 \
  --scale 5 \
  --fps 10
```
