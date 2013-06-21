MIBE (Machine Image Build Environment)
===

MIBE is a build environment for SmartOS images.

## Prerequisites
* [Add your SSH key to github] (https://help.github.com/articles/generating-ssh-keys)
* [Install SmartOS] (http://wiki.smartos.org/display/DOC/Download+SmartOS)
* [Install Pkgsrc] (http://wiki.smartos.org/display/DOC/Installing+pkgin)
* Install Git

        # pkgin install scmgit

* Import latest base/base64 image to build images from:

        # imgadm import $(imgadm avail | awk '/base64/ { print $1 }' | tail -1)

## Layout

* mi_home/bin - Holds scripts to handle repository operations and build images.

    * bin/repo_cloneall - Clones latest Joyent Machine Image repositories into mi_home/repos.
    * bin/repo_pullall - Pulls latest Joyent Machine Image repositories into mi_home/repos.
    * bin/repo_init - Initializes a new Machine Image repository and populates standard build files.
    * bin/tpl - Builds SmartOS images.

* mi_home/etc - Where configuration files for repositories are kept.

    * etc/repos.conf - Git server repository configuration on where to get Machine Image repos from.
    * etc/repos.list - Git repository list of Joyent Machine Image repositories. This is updated as more images are made public.

* mi_home/images - Final image dumps are stored here.
* mi_home/logs - Logging directory for image builds.
* mi_home/repos - Build repositories.


## Usage

Clone the mibe repository in /opt (or wherever has space to store image files):

    # cd /opt
    # git clone https://github.com/joyent/mibe
    # export PATH=$PATH:/opt/mibe/bin

Run repo_cloneall to grab the updated Joyent Machine Image build repositories.  They will be pulled down into mibe_home/repos.

    # repo_cloneall

Create a VM (SmartOS) for building images:

    # cat <<EOF > mibezone1.json
    {
      "brand": "joyent",
      "image_uuid": "9eac5c0c-a941-11e2-a7dc-57a6b041988f",
      "alias": "mibezone1",
      "hostname": "mibezone1",
      "max_physical_memory": 512,
      "quota": 20,
      "nics": [
        {
          "nic_tag": "admin",
          "ip": "dhcp",
          "primary": "true"
        }
      ]
    }
    EOF

Run vmadm create to create it:

    # vmadm create -f mibezone1.json

To build an example image we specify the base image as the uuid of base/base64 we imported above), the vm to use (uuid of mibezone1), and the repository build files:

    # cd /opt/mibe/repos
    # tpl -b 9eac5c0c-a941-11e2-a7dc-57a6b041988f -z 991e1640-d8f3-11e2-b7ca-9fde7e2f67c6 mi-example

The built image will be stored at mi_home/images/example-1.0.0.*
