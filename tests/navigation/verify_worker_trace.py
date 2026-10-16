"""Check diagnostic coverage against the actual production worker's host run."""
import subprocess
import sys

run = subprocess.run(sys.argv[1:], capture_output=True, text=True)
print(run.stdout, end='')
if run.stderr:
    print(run.stderr, file=sys.stderr, end='')
if run.returncode:
    raise SystemExit(run.returncode)
for reason in ('invalid_origin', 'invalid_destination', 'sd_not_ready',
               'mutex_allocation_failed', 'state_allocation_failed', 'mutex_busy',
               'task_create_failed', 'previous_job_retained', 'psram_unavailable'):
    assert f'event=rejected reason={reason}\n' in run.stdout, reason
for event in ('[Navigation][memory]', 'psram_largest=', 'internal_largest=',
              'usb_owner=', 'event=task_create', '[Navigation][map]',
              '[Navigation][phase]', '[Navigation][export]',
              'event=finish status=ready', 'event=finish status=cancelled',
              '[Navigation][save]', '[Navigation][cancel]'):
    assert event in run.stdout, event
assert 'latitude' not in run.stdout and 'longitude' not in run.stdout
assert 'Start' not in run.stdout and 'Finish' not in run.stdout
assert 'event=candidate path=/maps/tmap/osm/host.tmap' in run.stdout
assert 'event=candidate path=/maps/navigation/legacy.tmap' not in run.stdout
print('Production navigation trace admission/failure/lifecycle coverage PASS')
