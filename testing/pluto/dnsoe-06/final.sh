# you should see one RSA and on NULL only
grep -e IKEv2_AUTH_ -e ': Authenticated using ' /tmp/pluto.log
# NO ipsec tunnel should be up
ipsec whack --trafficstatus
: ==== cut ====
ipsec auto --status
: ==== tuc ====
../bin/check-for-core.sh
if [ -f /sbin/ausearch ]; then ausearch -r -m avc -ts recent ; fi
: ==== end ====
