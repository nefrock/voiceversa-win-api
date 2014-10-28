voiceversa-win-client (more specifically, the vvAPICpp project inside it) strongly depends on pjsip library for VoIP capabilities. Take the following steps to enable reference from vvAPICpp project.

1. Download the pjsip source code from [here](http://svn.pjsip.org/repos/pjproject).  
   (The version should be 2.2.1 or greater. We recommend 2.2.1.)

2. Open up the project with Microsoft Visual Studio.  
   (Visual Studio might ask whether you want to convert config files into newer versions. We recommend to say YES when asked.)

3. Follow the instructions at http://trac.pjsip.org/repos/wiki/Getting-Started/Windows

--------

* Although the document says 64-bit builds are not supported, it actually does on v2.2 or later.
* A .lib file will be created under the "lib" directory when you successfully complete the building steps of the above link.
