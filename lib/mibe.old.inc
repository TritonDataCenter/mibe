fail() {
	echo "$*" >&2;
	exit 1;
}
failtest() {
	printf "ERROR.\n\n! $*\n\n" >&2;
	zone_cleanup_sighandler;
	exit 1;
}
failts() {
	printf "ERROR.\n\n! $*\n\n" >&2;
	exit 1;
}

get_reposdir() {
	[[ "${HOME}" = "${mi_home}/users/${LOGNAME}" ]] && mi_reposdir="${mi_home}/users/${LOGNAME}" || mi_reposdir="${mi_home}/repos";
}
get_gitserver() {
	# Make sure mi_home/etc/repos.conf exists
	[[ ! -f "${mi_home}/etc/repos.conf" ]] && fail "* ERROR - ${mi_home}/etc/repos.conf doesn't exist.";

	# Grab GITSERVER setting from mi_home/etc/repos.conf
	mi_gitserver=$(grep "^GITSERVER=" ${mi_home}/etc/repos.conf | awk -F\" '{ print $2 }');

	# Make sure GITSERVER is not empty
	[[ ! -z "${mi_gitserver}" ]] || fail "* ERROR - Couldn't find git server in ${mi_home}/etc/repos.conf.";

	# Remove trailing slash if there is one
	[[ ! ${mi_gitserver} = */ ]] || mi_gitserver=$(echo "${mi_gitserver}" | gsed '$s/.$//');
}
get_reposlist() {
	# Make sure mi_home/etc/repos.list exists
	[[ ! -f "${mi_home}/etc/repos.list" ]] && fail "* ERROR - ${mi_home}/etc/repos.list doesn't exist.";

	# Grab repos.list
	mi_reposlist=$(cat ${mi_home}/etc/repos.list);
}
populate_smartos() {
	echo "* Populating ${mi_reposdir}/${mi_repo} SmartOS repository with build files..";
        [[ -d "${mi_reposdir}/${mi_repo}" ]] && fail "* ERROR - ${mi_reposdir}/${mi_repo} already exists.";
	mkdir ${mi_reposdir}/${mi_repo} &> /dev/null || fail "* ERROR - Couldn't create directory ${mi_reposdir}/${mi_repo}.";

	# Create Changelog
	cat > ${mi_reposdir}/${mi_repo}/Changelog <<-EOF
	${mi_image}-x.x.x
	* Base image is base64-x.x.x (20xxQx)
	* Initial release
	EOF

	# Create manifest
	cat > ${mi_reposdir}/${mi_repo}/manifest <<-EOF
	# Name of the image and version
	name="${mi_image}-x.x.x"

	# Build image to use for base of the image.  This image must
	# already exist in the ZFS hierarchy.
	base="base64-x.x.x"

	# Organization name (implanted in motd and product)
	organization="Joyent"

	# Brand name (implanted in motd and product)
	brand="Instance"

	# Description of the image
	description="Description for image ${mi_image}"

	# Homepage specific to the image
	homepage="http://wiki.joyent.com/jpc2/${mi_image}+Instance"

	# Metadata users to be created (not real users created)
	users=""

	# Role of image (database, loadbalancer, development, hadoop, other, os)
	role="development"

	# Minimum amount of ram needed
	minram=""
	EOF

	# Create customize
	cat > ${mi_reposdir}/${mi_repo}/customize <<-EOF
	#!/usr/bin/bash
	#
	# Put customizations to your image in this file.

	PATH=/opt/local/gnu/bin:/opt/local/bin:/opt/local/sbin:/usr/bin:/usr/sbin

	# Exit if any commands fail
	set -o errexit

	# Ensure we have updated standard packages
	echo "* Updating standard packages.";
	pkg_delete -v nodejs smtools zoneinit
	pkg_add -v nodejs smtools zoneinit
	npm install smartdc -g
	npm install jsontool -g
	npm install manta -g

	# Configuring image specific packages
	echo "* Configuring image specific packages.";

	# Clean up
	echo "* Cleaning up."
	rm -rf /root/*

	# Prepare image for provisioning
	sm-prepare-image -y
	EOF

	# Make customize executable
	chmod +x ${mi_reposdir}/${mi_repo}/customize

	# Create var/zoneinit/includes
	mkdir -p ${mi_reposdir}/${mi_repo}/copy/var/zoneinit/includes &> /dev/null || fail "* ERROR - Couldn't create directory ${mi_reposdir}/${mi_repo}/copy/var/zoneinit/includes.";

	# Create motd
	cat > ${mi_reposdir}/${mi_repo}/motd <<-EOF
   __        .                   .
 _|  |_      | .-. .  . .-. :--. |-
|_    _|     ;|   ||  |(.-' |  | |
  |__|   \`--'  \`-' \`;-| \`-' '  ' \`-'
                   /  ; %brand% (%name% %version%)
                   \`-'  %homepage%
	
	EOF

	# Create packages
	cat > ${mi_reposdir}/${mi_repo}/packages <<-EOF
	# List packages to add to your image one per line.
	dtracetools
	htop
	EOF

	# Create serverspec test files
	mkdir -p ${mi_reposdir}/${mi_repo}/tests/spec/${mi_image}

	cat > ${mi_reposdir}/${mi_repo}/tests/Rakefile <<-EOF
	require 'rake'
	require 'rspec/core/rake_task'
	require 'yaml'

	properties = YAML.load_file('properties.yml')

	desc "Run serverspec to all hosts"
	task :serverspec => 'serverspec:all'

	namespace :serverspec do
	  task :all => properties.keys.map {|key| 'serverspec:' + key.split('.')[0] }
	  properties.keys.each do |key|
	    desc "Run serverspec to #{key}"
	    RSpec::Core::RakeTask.new(key.split('.')[0].to_sym) do |t|
	      ENV['TARGET_HOST'] = key
	      t.pattern = 'spec/{' + properties[key][:roles].join(',') + '}/*_spec.rb'
	    end
	  end
	end
	EOF

	cat > ${mi_reposdir}/${mi_repo}/tests/spec/spec_helper.rb <<-EOF
	require 'serverspec'
	require 'pathname'
	require 'net/ssh'
	require 'yaml'

	include Serverspec::Helper::Ssh
	include Serverspec::Helper::DetectOS
	include Serverspec::Helper::Properties

	properties = YAML.load_file('properties.yml')

	RSpec.configure do |c|
	  c.host  = ENV['TARGET_HOST']
	  set_property properties[c.host]
	  options = Net::SSH::Config.for(c.host)
	  user    = options[:user] || Etc.getlogin
	  c.ssh   = Net::SSH.start(c.host, user, options)
	  c.os    = backend.check_os
	  #c.path  = '/usr/local/sbin:/usr/local/bin:/opt/local/sbin:/opt/local/bin:/usr/sbin:/usr/bin:/sbin'
	end
	EOF

	echo "* Done.";
}
init_repo() {
	echo "* Initializing repository ${mi_repo} at ${mi_reposdir}/${mi_repo}..";
	cd ${mi_reposdir}/${mi_repo} 
	git init 1> /dev/null || fail "* ERROR - Couldn't git init repository at ${mi_reposdir}/${mi_repo}.";

	git add .
	git commit -m "Initial commit from $0" 1> /dev/null || fail "* ERROR - Couldn't git commit files to ${mi_reposdir}/${mi_repo}.";
	git remote add origin ${mi_gitserver}:${mi_repo} 1> /dev/null || fail "* ERROR - Couldn't git remote add origin to ${mi_gitserver}:${mi_repo}.git";
	git push origin master 1> /dev/null || fail "* ERROR - Couldn't push files to ${mi_reposdir}/${mi_gitserver}:${mi_repo}.git";

	echo "* Done.";
}
get_vars() {
	mi_time=$(date +%Y-%m-%d_%H-%M-%S);
	mi_bzonet=$(echo ${mi_bzone:0:18});
	mi_bfinal_dump="${mi_home}/images/${name}.zfs.gz";
	mi_bfinal_manifest="${mi_home}/images/${name}.dsmanifest";
	mi_bfinal_snap="zones/${name}@final";
	mi_bimage_snap="zones/${base}@${name}";
	mi_bzone_tplate="zones/${name}";
	mi_brepo_path="${mi_reposdir}/${mi_brepo}";
	mi_bzone_logall="${mi_home}/logs/${name}-${mi_time}-all.log";
	mi_bzone_logerr="${mi_home}/logs/${name}-${mi_time}-error.log";
	mi_imagenn="${name%-*}";
	mi_imagenv="${name##*-}";
	mi_imagebn="${base%-*}";
	mi_imagebv="${base#*-}";
}
build_sanitycheck() {
	printf "\n%-60s" "* Sanity checking build files and environment..";

	# Check that the build zone mi_bzone exists
	[[ $(zoneadm -z ${mi_bzone} list &> /dev/null)$? -ne 0 ]] && failtest "Build zone ${mi_bzone} doesn\'t exist.";
	# Check if build zone mi_bzone is a SmartOS zone
	[[ $(zoneadm -z ${mi_bzone} list -p 2> /dev/null | awk -F: '{ print $6 }') = "kvm" ]] && failtest "Build zone ${mi_bzone} must be a SmartOS zone.";

	# Check that we have privileges to do things
	if [[ $(zonecfg -z ${mi_bzone} 'add attr; set name=mi-test; set type=string; set value="test"; end' 2> /dev/null)$? -ne 0 ]]; then
		failtest "Don't have proper privileges to continue.";
	else
		zonecfg -z ${mi_bzone} 'remove attr name=mi-test'
	fi

	# Check if build zone mi_bzone is configured and running
	[[ $(zoneadm -z ${mi_bzone} list -p 2> /dev/null | awk -F: '{ print $3 }') != "running" ]] && failtest "Build zone ${mi_bzone} must be configured and\n  running.";

	# Check that build repository mi_brepo directory exists
	[[ -d "${mi_brepo}" ]] || [[ -d "${mi_reposdir}/${mi_brepo}" ]] || failtest "Build repository ${mi_brepo} doesn\'t exist.";

	# Check that build repository manifest mi_brepo/manifest exists
	[[ -f "${mi_brepo}/manifest" ]] && source ${mi_brepo}/manifest || { [[ -f "${mi_reposdir}/${mi_brepo}/manifest" ]] && source ${mi_reposdir}/${mi_brepo}/manifest || failtest "A build repository manifest ${mi_brepo}/manifest doesn't exist.\n! A build repository with proper build files must exist for an\n! image to be built.  See https://github.com:joyent/mi-example\n"; }

	# Check that build repository customize mi_brepo/customize exists
	[[ -f "${mi_brepo}/customize" ]] || [[ -f "${mi_reposdir}/${mi_brepo}/customize" ]] || failtest "A build repository customize ${mi_brepo}/customize doesn\'t exist.\n! A build repository with proper build files must exist for an\n! image to be built.  See https://github.com:joyent/mi-example\n";

	# Get build variables after sourcing manifest
	get_vars;

	# Check that final build image file doesn't already exist
	[[ -f "${mi_bfinal_dump}" ]] && failtest "Build image ${mi_bfinal_dump} already exists\n! Nothing to do.";

	# Check if base image exists zones/name-version
	[[ $(zfs list -Ho name -t filesystem zones/${base} &> /dev/null)$? -ne 0 ]] && failtest "Base image zones/${base} doesn't exist.";

	# Check if mi_bimage_snap exists
	[[ $(zfs list -Ho name -t snapshot ${mi_bimage_snap} &> /dev/null)$? -eq 0 ]] && failtest "Base image snapshot ${mi_bimage_snap} already exists.";

	# Check if mi_bzone_tplate exists
	[[ $(zfs list -Ho name -t filesystem ${mi_bzone_tplate} &> /dev/null)$? -eq 0 ]] && failtest "Base template ${mi_bzone_tplate}.";

	printf "OK.\n";
}
zone_backup_zonepath() {
	mi_bzone_oldpath=$(eval echo $(zonecfg -z ${mi_bzone} info zonepath 2> /dev/null | awk -F: '{ print $2 }'));

	zonecfg -z ${mi_bzone} \
	"add attr; set name=mi-zpath; set type=string; set value=\"${mi_bzone_oldpath}\"; end" &> /dev/null \
	|| zonecfg -z ${mi_bzone} "select attr name=mi-zpath; set value=\"${mi_bzone_oldpath}\"; end" &> /dev/null;
}
zone_halt() {
	printf "%-60s" "* Halting build zone (${mi_bzonet})..";
	zoneadm -z ${mi_bzone} halt &> /dev/null || failtest "Unable to halt zone ${mi_bzone}";

	zone_timeout="30";
	while true; do
		[[ ! $(pgrep -f '^zoneadmd.*'${mi_bzone}'$' &> /dev/null) ]] && break;
		sleep 1;

		zone_timeout=$((zone_timeout - 1));
		[[ ${zone_timeout} -le 0 ]] && failtest "Unable to halt zone ${mi_bzone}."; 
	done
	sleep 1;

	printf "OK.\n";
}
zone_configure_buildzone() {
	printf "%-60s" "* Configuring build zone (${mi_bzonet})..";

	# Unmount cores filesystem zones/cores/mi_bzone from build zone mi_bzone
	zfs unmount zones/cores/${mi_bzone} || failtest "Unable to unmount zones/cores/${mi_bzone}.";

	# Detach existing dataset from build zone mi_bzone
	zoneadm -z ${mi_bzone} detach 2> /dev/null || failtest "Unable to detach dataset from ${mi_bzone}.";

	# Snapshot the build (base) image mi_bimage as zones/mi_bimage@name-version
	zfs snapshot ${mi_bimage_snap} &> /dev/null || failtest "Unable to snapshot ${mi_bimage_snap}.";

	# Clone the snapshot above mi_bimage_snap into build image template mi_bzone_tplate (zones/name-version)
	zfs clone ${mi_bimage_snap} ${mi_bzone_tplate} &> /dev/null || failtest "Unable to clone ${mi_bimage_snap} to ${mi_bzone_tplate}.";

	# Re-configure build zone mi_bzone with build image template mi_bzone_tplate
	zonecfg -z ${mi_bzone} -f - &> /dev/null <<-EOF
	set zonepath=/${mi_bzone_tplate}
	EOF

	# Attach build zone mi_bzone to mi_bzone_tplate (zones/name-version)
	zoneadm -z ${mi_bzone} attach -F &> /dev/null || failtest "Unable to attach build zone ${mi_bzone} to ${mi_bzone_tplate}.";

	# Configure networking for build zone mi_bzone
	mi_bzone_vnics=($(zonecfg -z ${mi_bzone} info net 2>/dev/null | \
	  awk '{if($1~/physical/) print $2}'));

	mi_bzone_oldpath=$(eval echo $(zonecfg -z ${mi_bzone} \
	  info attr name=mi-zpath 2>/dev/null | \
	  awk -F: '{if ($1~/value/) print $2}'));

	mv /${mi_bzone_tplate}/root/etc/defaultrouter{,.orig} 2> /dev/null || true;
	cp -p ${mi_bzone_oldpath}/root/etc/defaultrouter \
	  /${mi_bzone_tplate}/root/etc/defaultrouter 2> /dev/null || true;

	for mi_bzone_vnic in ${mi_bzone_vnics[@]}; do
	  mv /${mi_bzone_tplate}/root/etc/hostname.${mi_bzone_vnic}{,.orig} 2> /dev/null || true;
	  cp -p ${mi_bzone_oldpath}/root/etc/hostname.${mi_bzone_vnic} \
	    /${mi_bzone_tplate}/root/etc/hostname.${mi_bzone_vnic} 2> /dev/null || true;
	done

	printf "OK.\n";
}
zone_copyin_files() {
	OLDIFS=$IFS;
	IFS=$(echo -en "\n\b");
	mi_bzone_copydirs=$(ls -d ${mi_brepo_path}/copy/*/ 2>/dev/null || true);
	if [[ ! -z ${mi_bzone_copydirs} ]]; then
		printf "%-60s" "* Copying in ${mi_brepo}/copy files..";

		(cd ${mi_brepo_path}/copy && gtar -c . | gtar -C /${mi_bzone_tplate}/root -x);
		for mi_bzone_copydir in ${mi_bzone_copydirs[@]}; do
			mi_bzone_copydir_files=$(find ${mi_bzone_copydir} | egrep -v "/copy$");
			for mi_bzone_copydir_file in ${mi_bzone_copydir_files[@]}; do
				mi_bzone_copydir_filet=$(echo "${mi_bzone_copydir_file}" | nawk -F"copy" '{ print $2 }');  
				chown root:root /${mi_bzone_tplate}/root/${mi_bzone_copydir_filet}
			done
		done
		printf "OK.\n";
	fi
	IFS=${OLDIFS};
}
zone_boot() {
	printf "%-60s" "* Booting build zone (${mi_bzonet})..";

	zoneadm -z ${mi_bzone} boot
# &> /dev/null || failtest "Unable to boot ${mi_bzone}.";

	zone_timeout="80";
	while true; do
		mi_bzone_state=$(svcs -Ho state -z ${mi_bzone} milestone/multi-user-server 2>/dev/null || true);
		[[ "${mi_bzone_state}" == "online" ]] && break;
		sleep 1;

		zone_timeout=$((zone_timeout - 1));
		[[ ${zone_timeout} -le 0 ]] && failtest "Unable to boot zone ${mi_bzone}."; 
	done

	printf "OK.\n";
}
zone_create_motdproduct() {
        if [[ -f "/${mi_brepo_path}/motd" ]]; then
		printf "%-60s" "* Creating image motd and product file..";
                sed -e "s!%brand%!${brand}!g" \
                    -e "s!%name%!${mi_imagenn}!g" \
                    -e "s!%version%!${mi_imagenv}!g" \
                    -e "s!%homepage%!${homepage}!g" /${mi_brepo_path}/motd > /${mi_bzone_tplate}/root/etc/motd

		cat > /${mi_bzone_tplate}/root/etc/product <<-EOF
		Name: ${organization} ${brand}
		Image: ${mi_imagenn} ${mi_imagenv}
		Base Image: ${mi_imagebn} ${mi_imagebv}
		Documentation: ${homepage}
		Description: ${description}
		EOF
	else
		printf "%-60s" "* Creating image product file..";

		cat > /${mi_bzone_tplate}/root/etc/product <<-EOF
		Name: ${organization} ${brand}
		Image: ${mi_imagenn} ${mi_imagenv}
		Base Image: ${mi_imagebn} ${mi_imagebv}
		Documentation: ${homepage}
		Description: ${description}
		EOF
        fi

	printf "OK.\n";
}
zone_install_packages() {
	# First run pkgin update
	mi_bzone_result=$(zlogin ${mi_bzone} 'PATH=/opt/local/bin:/opt/local/sbin:/usr/bin:/usr/sbin \
            touch /opt/local/.dlj_license_accepted; pkgin -f -y up 2>&1' >> ${mi_bzone_logall} \
            | tee -a ${mi_bzone_logall} >> ${mi_bzone_logerr} || echo $?);

	[[ ! ${mi_bzone_result} ]] || failtest "Unable to update pkgin list.";

	if [[ -f "${mi_brepo_path}/packages" ]]; then
    		IFS=$'\n\r';
		mi_bzone_packagesr=($(cat ${mi_brepo_path}/packages));
		unset IFS;
		mi_bzone_packages=(${mi_bzone_packagesr[@]/\#*/});

		if [[ ! -z "${mi_bzone_packages}" ]]; then
			printf "%-60s" "* Installing packages list..";

			echo "pkg_add -v ${mi_bzone_packages[@]}" >> ${mi_bzone_logall}

			mi_bzone_result=$(zlogin ${mi_bzone} \
			  PATH=/opt/local/bin:/opt/local/sbin:/usr/bin:/usr/sbin \
			  pkg_add -v ${mi_bzone_packages[@]} 2>&1 >> ${mi_bzone_logall} \
			  | tee -a ${mi_bzone_logall} >> ${mi_bzone_logerr} || echo $?); 

			[[ ! ${mi_bzone_result} ]] && printf "OK.\n" || failtest "There was an error installing the packages list.\n! See ${mi_bzone_logerr}.";
		fi
	fi
		
}
zone_exec_customize() {
	if [[ -f "${mi_brepo_path}/customize" ]]; then
		printf "%-60s" "* Executing the customize file..";

		cp /${mi_brepo_path}/customize /${mi_bzone_tplate}/root/root
		chmod +x /${mi_bzone_tplate}/root/root/customize

		mi_bzone_result=$(zlogin ${mi_bzone} 'bash -v /root/customize 2>&1' >> ${mi_bzone_logall} \
		  | tee -a ${mi_bzone_logall} >> ${mi_bzone_logerr} || echo $?);

		[[ ! ${mi_bzone_result} ]] && printf "OK.\n" || failtest "There was an error during execution of the customize file.\n! See ${mi_bzone_logerr}.";
	fi
}
zone_cleanup() {
        # Check if mi_bzone_tplate exists and remove it
        [[ $(zfs list -Ho name -t filesystem ${mi_bzone_tplate} &> /dev/null)$? -eq 0 ]] && zfs destroy -R ${mi_bzone_tplate} &> /dev/null

	# Check if mi_bimage_snap exists and remove it
        [[ $(zfs list -Ho name -t snapshot ${mi_bimage_snap} &> /dev/null)$? -eq 0 ]] && zfs destroy ${mi_bimage_snap} &> /dev/null
}
zone_cleanup_sighandler() {
	printf "\n* Cleaning up..\n\n";

	zoneadm -z ${mi_bzone} halt &> /dev/null || true;

	# Detach existing dataset from build zone mi_bzone
	zoneadm -z ${mi_bzone} detach &> /dev/null || true;

	# Re-configure build zone mi_bzone with original dataset
	zonecfg -z ${mi_bzone} "set zonepath=/zones/${mi_bzone}" &> /dev/null || true;

	# Re-attach the original dataset to the build zone mi_bzone
	zoneadm -z ${mi_bzone} attach -F &> /dev/null || true;

	# Mount the cores filesystem back to the build zone mi_bzone
	zfs mount zones/cores/${mi_bzone} &> /dev/null || true;

	# Clear the mi-zpath setting from build zone mi_bzone
	zonecfg -z ${mi_bzone} 'remove attr name=mi-zpath' &> /dev/null || true;

	# Boot mi_bzone
	zoneadm -z ${mi_bzone} boot &> /dev/null || true;

        # Check if mi_bzone_tplate exists and remove it
        [[ $(zfs list -Ho name -t filesystem ${mi_bzone_tplate} &> /dev/null)$? -eq 0 ]] && zfs destroy -R ${mi_bzone_tplate} &> /dev/null

	# Check if mi_bimage_snap exists and remove it
        [[ $(zfs list -Ho name -t snapshot ${mi_bimage_snap} &> /dev/null)$? -eq 0 ]] && zfs destroy ${mi_bimage_snap} &> /dev/null

	exit 1;
}
zone_create_dumpmanifest() {
	printf "%-60s" "* Creating image file and manifest..";

	zfs send ${mi_bzone_tplate}@final 2> /dev/null | gzip -9 > ${mi_bfinal_dump} || failtest "Unable to zfs dump ${mi_bfinal_dump}.";

  # Create users block for manifest
  for user in ${users}; do
    mi_manifest_users_snippet="${mi_manifest_users_snippet+${mi_manifest_users_snippet}, }{\"name\": \"${user}\"}";
  done

  if [ -n "${mi_manifest_users_snippet}" ]; then
    mi_manifest_users_block="\"users\": [ ${mi_manifest_users_snippet} ],
  \"generate_passwords\": true,";
  else
    mi_manifest_users_block='"users": [ {"name": "root"}, {"name": "admin"} ],
  "generate_passwords": true,';
  fi

  # Create minram block for manifest if exists
  if [ -n "${minram}" ]; then
    mi_manifest_minram_block="\"min_ram\": ${minram},"
  fi

  # Create role block for manifest
  if [ -n "${role}" ] && [ -n "${lbaas}" ]; then
    mi_manifest_role_block="\"tags\": { \"role\": \"${role}\", \"lbaas\": ${lbaas} },";
  elif [ -n "${role}" ]; then
    mi_manifest_role_block="\"tags\": { \"role\": \"${role}\" },";
  fi

  # Create billing tags block for manifest if exists
  if [ -n "${billingtags}" ]; then
    mi_manifest_billingtags_block="\"billing_tags\": [ \"${billingtags}\" ],";
  fi

  # Create homepage tags block for manifest if exists
  if [ -n "${homepage}" ]; then
    mi_manifest_homepage_block="\"homepage\": \"${homepage}\",";
  fi

  # Create networks block for manifest if exists
  for net in ${networks}; do
    mi_manifest_networks_snippet="${mi_manifest_networks_snippet+${mi_manifest_networks_snippet}, }{\"name\": \"${net}\", \"description\": \"${!net}\"}";
  done
  if [ -n "${mi_manifest_networks_snippet}" ]; then
    mi_manifest_networks_block="\"networks\": [ ${mi_manifest_networks_snippet} ]";
  else
    mi_manifest_networks_block='"networks": [ {"name": "net0", "description": "public"} ]';
  fi

  # Set manifest owner_uuid to 9dce1460-0c4c-4417-ab8b-25ca478c5a78 if not set in manifest
  [[ -z ${owneruuid} ]] && mi_manifest_owneruuid="9dce1460-0c4c-4417-ab8b-25ca478c5a78"; 

  # Get file details
  mi_file_time=$(ls -l --time-style '+%Y-%m-%dT%H:%M:%SZ' ${mi_bfinal_dump} | awk '{ print $6 }');
  mi_file_hash=$(digest -a sha1 ${mi_bfinal_dump});
  mi_file_size=$(ls -l ${mi_bfinal_dump} | awk '{ print $5 }');
  mi_manifest_uuid=$(uuid);

sed '/^[[:blank:]]*$/d' <<-EOF > ${mi_bfinal_manifest}
{
  "v": "2",
  "uuid": "${mi_manifest_uuid}",
  "owner": "${mi_manifest_owneruuid}",
  "name": "${mi_imagenn}",
  "version": "${mi_imagenv}",
  "state": "active",
  "disabled": false,
  "public": false,
  "published_at": "${mi_file_time}",
  "type": "zone-dataset",
  "os": "smartos",
  "files": [
    { 
      "sha1": "${mi_file_hash}",
      "size": ${mi_file_size},
      "compression": "gzip"
    }
  ],
  "description": "${description}",
  ${mi_manifest_homepage_block}
  "requirements": {
    ${mi_manifest_minram_block}
    ${mi_manifest_networks_block}
  },
  ${mi_manifest_users_block}
  ${mi_manifest_role_block}
  ${mi_manifest_billingtags_block}
  "urn": "sdc:sdc:${mi_imagenn}:${mi_imagenv}"
}
EOF
	printf "OK.\n\n";
}
zone_unconfigure_buildzone() {
	printf "%-60s" "* Un-configuring build zone (${mi_bzonet})..";
	
	# Detach existing dataset from build zone mi_bzone
	zoneadm -z ${mi_bzone} detach &> /dev/null || failtest "Unable to detach dataset from ${mi_bzone}.";

	# Restore original network configuration to build zone mi_bzone
	mi_bzone_vnics=($(zonecfg -z ${mi_bzone} info net 2>/dev/null | \
	  awk '{if($1~/physical/) print $2}'));

	mv /${mi_bzone_tplate}/root/etc/defaultrouter{.orig,} 2>/dev/null || \
	rm /${mi_bzone_tplate}/root/etc/defaultrouter 2>/dev/null || true;

	for mi_bzone_vnic in ${mi_bzone_vnics[@]}; do
		mv /${mi_bzone_tplate}/root/etc/hostname.${mi_bzone_vnic}{.orig,} 2>/dev/null || \
		rm /${mi_bzone_tplate}/root/etc/hostname.${mi_bzone_vnic} 2>/dev/null || true;
	done

	# Snapshot the final image mi_bzone_tplate@final
	# Make sure the final snapshot doesn't contain any SUNattached.xml/SUNWdetached.xml files
	rm -f /${mi_bzone_tplate}/SUNW{at,de}tached.xml
	zfs snapshot ${mi_bzone_tplate}@final &> /dev/null || failtest "Unable to create snapshot ${mi_bzone_tplate}@final.";

	# Re-configure build zone mi_bzone with original dataset
	mi_bzone_oldpath=$(eval echo $(zonecfg -z ${mi_bzone} \
	info attr name=mi-zpath 2>/dev/null | awk -F: '{if ($1~/value/) print $2}'));

	zonecfg -z ${mi_bzone} "set zonepath=${mi_bzone_oldpath}" &> /dev/null

	# Re-attach the original dataset to the build zone mi_bzone
	zoneadm -z ${mi_bzone} attach -F &> /dev/null || failtest "Unable to attach original dataset to ${mi_bzone}.";

	# Mount the cores filesystem back to the build zone mi_bzone
	mi_bzone_path=$(eval echo $(zonecfg -z ${mi_bzone} info zonepath 2>/dev/null | awk -F: '{print $2}')); 

	zfs mount zones/cores/${mi_bzone} &> /dev/null || true;

	# Clear the mi-zpath setting from build zone mi_bzone
	zonecfg -z ${mi_bzone} 'remove attr name=mi-zpath' 2>/dev/null || true;

	# Boot mi_bzone
	zoneadm -z ${mi_bzone} boot &> /dev/null || failtest "Unable to boot ${mi_bzone}.";

	printf "OK.\n";
}
