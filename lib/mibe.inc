fail() {
	echo "$*" >&2;
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
get_reposlistlocal() {
	# Make sure mi_home/etc/repos_local.list exists
	[[ ! -f "${mi_home}/etc/repos_local.list" ]] && fail "* ERROR - ${mi_home}/etc/repos_local.list doesn't exist.";

	# Grab repos.list
	mi_reposlist=$(cat ${mi_home}/etc/repos_local.list);
}
get_gitserverlocal() {
	# Make sure mi_home/etc/repos_local.conf exists
	[[ ! -f "${mi_home}/etc/repos_local.conf" ]] && fail "* ERROR - ${mi_home}/etc/repos_local.conf doesn't exist.";

	# Grab GITSERVER setting from mi_home/etc/repos_local.conf
	mi_gitserver=$(grep "^GITSERVER=" ${mi_home}/etc/repos_local.conf | awk -F\" '{ print $2 }');

	# Make sure GITSERVER is not empty
	[[ ! -z "${mi_gitserver}" ]] || fail "* ERROR - Couldn't find git server in ${mi_home}/etc/repos_local.conf.";

	# Remove trailing slash if there is one
	[[ ! ${mi_gitserver} = */ ]] || mi_gitserver=$(echo "${mi_gitserver}" | gsed '$s/.$//');
}
populate_smartos() {
	echo "* Populating ${mi_reposdir}/${mi_repo} SmartOS repository with build (tpl) files..";
        [[ -d "${mi_reposdir}/${mi_repo}" ]] && fail "* ERROR - ${mi_reposdir}/${mi_repo} already exists.";
	mkdir ${mi_reposdir}/${mi_repo} &> /dev/null || fail "* ERROR - Couldn't create directory ${mi_reposdir}/${mi_repo}.";

	# Create standard tpl Changelog
	cat > ${mi_reposdir}/${mi_repo}/Changelog <<-EOF
	# Changelog for ${mi_repo}

	${mi_image}-1.0.0
	* Change1
	* Change2
	* etc
	EOF

	# Create standard tpl manifest
	cat > ${mi_reposdir}/${mi_repo}/manifest <<-EOF
	name="${mi_image}"
	organization="Joyent"
	brand="Instance"
	version="1.0.0"
	description="Description for image ${mi_image}"
	homepage="http://wiki.joyent.com/jpc2/Joyent+${mi_image}+SmartMachine"
	users="root admin"
	EOF

	# Create standard tpl customize
	cat > ${mi_reposdir}/${mi_repo}/customize <<-EOF
	#!/usr/bin/bash
	#
	# Put customizations to your image in this file.

	PATH=/opt/local/gnu/bin:/opt/local/bin:/opt/local/sbin:/usr/bin:/usr/sbin

	# Exit if any commands fail
	set -o errexit

	# Ensure we have updated standard packages
	echo "* Updating standard packages.";
	pkg_delete -v nodejs smtools
	pkg_add -v nodejs smtools dtracetools htop
	npm install smartdc -g
	npm install jsontool -g

	# Configuring image specific packages
	echo "* Configuring image specific packages.";

	# Clean up
	echo "* Cleaning up."
	rm -rf /root/*

	# Prepare image for provisioning
	sm-prepare-image -y
	EOF

	# Make standard tpl customize executable
	chmod +x ${mi_reposdir}/${mi_repo}/customize

	# Create standard tpl var/zoneinit/includes
	mkdir -p ${mi_reposdir}/${mi_repo}/copy/var/zoneinit/includes &> /dev/null || fail "* ERROR - Couldn't create directory ${mi_reposdir}/${mi_repo}/copy/var/zoneinit/includes.";

	# Create standard tpl motd
	cat > ${mi_reposdir}/${mi_repo}/motd <<-EOF
   __        .                   .
 _|  |_      | .-. .  . .-. :--. |-
|_    _|     ;|   ||  |(.-' |  | |
  |__|   \`--'  \`-' \`;-| \`-' '  ' \`-'
                   /  ; %brand% (%name% %version%)
                   \`-'  %homepage%
	
	EOF

	# Create standard tpl packages
	cat > ${mi_reposdir}/${mi_repo}/packages <<-EOF
	# List packages to add to your image here one per line.
	EOF

	echo "* Done.";
}
init_repo() {
	echo "* Initializing repository ${mi_repo} at ${mi_reposdir}/${mi_repo}..";
	cd ${mi_reposdir}/${mi_repo} 
	git init 1> /dev/null || fail "* ERROR - Couldn't git init repository at ${mi_reposdir}/${mi_repo}.";

	git add .
	git commit -m "Initial commit from $0" 1> /dev/null || fail "* ERROR - Couldn't git commit files to ${mi_reposdir}/${mi_repo}.";
	git remote add origin ${mi_gitserver}:${mi_repo}.git 1> /dev/null || fail "* ERROR - Couldn't git remote add origin to ${mi_gitserver}:${mi_repo}.git";
	git push origin master 1> /dev/null || fail "* ERROR - Couldn't push files to ${mi_reposdir}/${mi_gitserver}:${mi_repo}.git";

	echo "* Done.";
}
