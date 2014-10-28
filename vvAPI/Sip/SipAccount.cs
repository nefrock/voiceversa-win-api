using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

using Sipek.Common;

namespace Sipek.Sip
{
    public class SipAccount : IAccount
    {
        bool _enabled;
        int _index;
        string _accountName;
        string _hostName;
        string _id;
        string _userName;
        string _password;
        string _displayName;
        string _domainName;
        int _regState;
        string _proxyAddress;
        ETransportMode _transportMode;

        public SipAccount()
        {
            _enabled = true;
            _index = 0;
            _accountName = "";
            _hostName = "";
            _id = "";
            _userName = "";
            _password = "";
            _displayName = "";
            _domainName = "";
            _regState = 0;
            _proxyAddress = "";
            _transportMode = ETransportMode.TM_UDP;
        }

        public bool Enabled
        {
            get { return _enabled; }
            set { _enabled = value; }
        }

        public int Index
        {
            get { return _index; }
            set { _index = value; }
        }


        public string AccountName
        {
            get { return _accountName; }
            set { _accountName = value; }
        }

        public string HostName
        {
            get { return _hostName; }
            set { _hostName = value; }
        }

        public string Id
        {
            get { return _id; }
            set { _id = value; }
        }

        public string UserName
        {
            get { return _userName; }
            set { _userName = value; }
        }

        public string Password
        {
            get { return _password; }
            set { _password = value; }
        }

        public string DisplayName
        {
            get { return _displayName; }
            set { _displayName = value; }
        }

        public string DomainName
        {
            get { return _domainName; }
            set { _domainName = value; }
        }

        public int RegState
        {
            get { return _regState; }
            set { _regState = value; }
        }

        public string ProxyAddress
        {
            get { return _proxyAddress; }
            set { _proxyAddress = value; }
        }

        public ETransportMode TransportMode
        {
            get { return _transportMode; }
            set { _transportMode = value; }
        }
    }
}
