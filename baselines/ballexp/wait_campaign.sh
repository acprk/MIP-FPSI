#!/bin/bash
# Poll every 5 minutes until the campaign follow-up has finished.
F=<ARTIFACT_ROOT>/results/campaign2_20261023_1344/env.txt
while ! grep -q "^followup done=" $F; do sleep 300; done
grep "^followup done=" $F
date
