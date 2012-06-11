//-----------------------------------------------------------------------------------------
#if 0

Fix8 is released under the New BSD License.

Copyright (c) 2010-12, David L. Dight <fix@fix8.org>
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of
	 	conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list
	 	of conditions and the following disclaimer in the documentation and/or other
		materials provided with the distribution.
    * Neither the name of the author nor the names of its contributors may be used to
	 	endorse or promote products derived from this software without specific prior
		written permission.
    * Products derived from this software may not be called "Fix8", nor can "Fix8" appear
	   in their name without written permission from fix8.org

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS
OR  IMPLIED  WARRANTIES,  INCLUDING,  BUT  NOT  LIMITED  TO ,  THE  IMPLIED  WARRANTIES  OF
MERCHANTABILITY AND  FITNESS FOR A PARTICULAR  PURPOSE ARE  DISCLAIMED. IN  NO EVENT  SHALL
THE  COPYRIGHT  OWNER OR  CONTRIBUTORS BE  LIABLE  FOR  ANY DIRECT,  INDIRECT,  INCIDENTAL,
SPECIAL,  EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING,  BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE  GOODS OR SERVICES; LOSS OF USE, DATA,  OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED  AND ON ANY THEORY OF LIABILITY, WHETHER  IN CONTRACT, STRICT  LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#endif
//-----------------------------------------------------------------------------------------
#include <iostream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <iterator>
#include <memory>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <bitset>

#include <strings.h>
#include <regex.h>

#include <f8includes.hpp>

//-------------------------------------------------------------------------------------------------
using namespace FIX8;
using namespace std;

//-------------------------------------------------------------------------------------------------
RegExp MessageBase::_elmnt("([0-9]+)=([^\x01]+)\x01");
RegExp Message::_hdr("8=([^\x01]+)\x01{1}9=([^\x01]+)\x01{1}(35=)([^\x01]+)\x01");
RegExp Message::_tlr("(10=)([^\x01]+)\x01");

//-------------------------------------------------------------------------------------------------
namespace {
	const string spacer(3, ' ');
}

//-------------------------------------------------------------------------------------------------
unsigned MessageBase::decode(const f8String& from, const unsigned offset)
{
	RegMatch match;
	unsigned s_offset(offset);
	f8String tag, val;
	const unsigned fsize(from.size());

	for (unsigned pos(_pos.size()); s_offset < fsize && _elmnt.SearchString(match, from, 3, s_offset) == 3; )
	{
		_elmnt.SubExpr(match, from, tag, s_offset, 1);
		_elmnt.SubExpr(match, from, val, s_offset, 2);
		const unsigned tv(fast_atoi<unsigned>(tag.c_str()));
		const BaseEntry *be(_ctx._be.find_ptr(tv));
#if defined PERMIT_CUSTOM_FIELDS
		if (!be && (!_ctx._ube || (be = _ctx._ube->find_ptr(tv)) == 0))
#else
		if (!be)
#endif
			throw InvalidField(tv);
		Presence::const_iterator itr(_fp.get_presence().end());
		if (!_fp.has(tv, itr))
			break;
		s_offset += match.SubSize();
		if (_fp.get(tv, itr, FieldTrait::present))
		{
			if (!_fp.get(tv, itr, FieldTrait::automatic))
				throw DuplicateField(tv);
		}
		else
		{
			add_field(tv, itr, ++pos, be->_create(val, be->_rlm), false);
			if (_fp.is_group(tv, itr))
				s_offset = decode_group(tv, from, s_offset);
		}
	}

	const unsigned short missing(_fp.find_missing());
	if (missing)
	{
		const BaseEntry& tbe(_ctx._be.find_ref(missing));
		ostringstream ostr;
		ostr << tbe._name << " (" << missing << ')';
		throw MissingMandatoryField(ostr.str());
	}

	return s_offset;
}

//-------------------------------------------------------------------------------------------------
unsigned MessageBase::decode_group(const unsigned short fnum, const f8String& from, const unsigned offset)
{
	unsigned s_offset(offset);
	GroupBase *grpbase(find_group(fnum));
	if (!grpbase)
		throw InvalidRepeatingGroup(fnum);
	const unsigned fsize(from.size());

	for (bool ok(true); ok && s_offset < fsize; )
	{
		RegMatch match;
		scoped_ptr<MessageBase> grp(grpbase->create_group());

		for (unsigned pos(0); s_offset < fsize && _elmnt.SearchString(match, from, 3, s_offset) == 3; )
		{
			f8String tag, val;
			_elmnt.SubExpr(match, from, tag, s_offset, 1);
			_elmnt.SubExpr(match, from, val, s_offset, 2);
			const unsigned tv(fast_atoi<unsigned>(tag.c_str()));
			Presence::const_iterator itr(grp->_fp.get_presence().end());
			if (grp->_fp.get(tv, itr, FieldTrait::present))	// already present; next group?
				break;
			if (pos == 0 && grp->_fp.getPos(tv, itr) != 1)	// first field in group is mandatory
				throw MissingRepeatingGroupField(tv);
			const BaseEntry *be(_ctx._be.find_ptr(tv));
			if (!be)
				throw InvalidField(tv);
			if (!grp->_fp.has(tv, itr))	// field not found in sub-group - end of repeats?
			{
				ok = false;
				break;
			}
			s_offset += match.SubSize();
			grp->add_field(tv, itr, ++pos, be->_create(val, be->_rlm), false);
			grp->_fp.set(tv, itr, FieldTrait::present);	// is present
			if (grp->_fp.is_group(tv, itr)) // nested group
				s_offset = grp->decode_group(tv, from, s_offset);
		}

		const unsigned short missing(grp->_fp.find_missing());
		if (missing)
		{
			const BaseEntry& tbe(_ctx._be.find_ref(missing));
			ostringstream ostr;
			ostr << tbe._name << " (" << missing << ')';
			throw MissingMandatoryField(ostr.str());
		}
		*grpbase += grp.release();
	}

	return s_offset;
}

//-------------------------------------------------------------------------------------------------
unsigned MessageBase::check_positions()
{
	return 0;
}

//-------------------------------------------------------------------------------------------------
unsigned Message::decode(const f8String& from)
{
	unsigned offset(_header->decode(from, 0));
	offset = MessageBase::decode(from, offset);
	return _trailer->decode(from, offset);
}

//-------------------------------------------------------------------------------------------------
Message *Message::factory(const F8MetaCntx& ctx, const f8String& from)
{
	RegMatch match;
	Message *msg(0);
	if (_hdr.SearchString(match, from, 5, 0) == 5)
	{
		f8String len, mtype;
		_hdr.SubExpr(match, from, len, 0, 2);
		_hdr.SubExpr(match, from, mtype, 0, 4);
		const unsigned mlen(fast_atoi<unsigned>(len.c_str()));

#if defined CODECTIMING
		ostringstream gerr;
		gerr << "  ctor(" << mtype << "):";
		IntervalTimer itm;
#endif
		const BaseMsgEntry *bme(ctx._bme.find_ptr(mtype));
		if (!bme)
			throw InvalidMessage(mtype);
		msg = bme->_create();
#if defined PERMIT_CUSTOM_FIELDS
		if (ctx._ube)
			ctx._ube->post_msg_ctor(msg);
#endif
#if defined CODECTIMING
		gerr << itm.Calculate();
		GlobalLogger::log(gerr.str());
		gerr.str("");
		gerr << "decode(" << mtype << "):";
#endif
		msg->decode(from);
#if defined CODECTIMING
		gerr << itm.Calculate();
		GlobalLogger::log(gerr.str());
#endif

		Fields::const_iterator fitr(msg->_header->_fields.find(Common_BodyLength));
		static_cast<body_length *>(fitr->second)->set(mlen);
		fitr = msg->_header->_fields.find(Common_MsgType);
		static_cast<msg_type *>(fitr->second)->set(mtype);
		msg->check_set_rlm(fitr->second);

		if (_tlr.SearchString(match, from, 3, 0) == 3)
		{
			f8String chksum;
			_hdr.SubExpr(match, from, chksum, 0, 2);

			Fields::const_iterator fitr(msg->_trailer->_fields.find(Common_CheckSum));
			static_cast<check_sum *>(fitr->second)->set(chksum);
			const unsigned chkval(fast_atoi<unsigned>(chksum.c_str())), // chksum value
				mchkval(calc_chksum(from, 0, match.SubPos(1))); // chksum pos
			if (chkval != mchkval)
				throw BadCheckSum(mchkval);
		}
	}
	else
		throw InvalidMessage(from);

	return msg;
}

//-------------------------------------------------------------------------------------------------
// copy all fields from this message to 'to' where the field is legal for 'to' and it is not
// already present in 'to'; includes repeating groups;
// if force, copy all fields regardless, replacing any existing, adding any new
unsigned MessageBase::copy_legal(MessageBase *to, bool force) const
{
	unsigned copied(0);
	for (Presence::const_iterator itr(_fp.get_presence().begin()); itr != _fp.get_presence().end(); ++itr)
	{
		if (itr->_field_traits & FieldTrait::present && (force || (to->_fp.has(itr->_fnum) && !to->_fp.get(itr->_fnum))))
		{
			if (itr->_field_traits & FieldTrait::group)
			{
				GroupBase *gb(find_group(itr->_fnum)), *gb1(to->find_group(itr->_fnum));

				for (GroupElement::const_iterator gitr(gb->_msgs.begin()); gitr != gb->_msgs.end(); ++gitr)
				{
					MessageBase *grc(gb1->create_group());
					(*gitr)->copy_legal(grc, force);
					*gb1 += grc;
				}
			}

			BaseField *nf(get_field(itr->_fnum)->copy());
			to->check_set_rlm(nf);
			Presence::const_iterator fpitr(_fp.get_presence().end());
			if (force && to->_fp.get(itr->_fnum, fpitr, FieldTrait::present))
				delete to->replace(itr->_fnum, fpitr, nf);
			else
				to->add_field(nf);
			++copied;
		}
	}

	return copied;
}

//-------------------------------------------------------------------------------------------------
unsigned MessageBase::encode(ostream& to) const
{
	const std::ios::pos_type where(to.tellp());
	for (Positions::const_iterator itr(_pos.begin()); itr != _pos.end(); ++itr)
	{
		check_set_rlm(itr->second);
		Presence::const_iterator fpitr(_fp.get_presence().end());
		if (!_fp.get(itr->second->_fnum, fpitr, FieldTrait::suppress))	// some fields are not encoded until unsuppressed (eg. checksum)
		{
			itr->second->encode(to);
			if (_fp.get(itr->second->_fnum, fpitr, FieldTrait::group))
				encode_group(itr->second->_fnum, to);
		}
	}

	return to.tellp() - where;
}

//-------------------------------------------------------------------------------------------------
unsigned MessageBase::encode_group(const unsigned short fnum, std::ostream& to) const
{
	const std::ios::pos_type where(to.tellp());
	GroupBase *grpbase(find_group(fnum));
	if (!grpbase)
		throw InvalidRepeatingGroup(fnum);
	for (GroupElement::iterator itr(grpbase->_msgs.begin()); itr != grpbase->_msgs.end(); ++itr)
		(*itr)->encode(to);
	return to.tellp() - where;
}

//-------------------------------------------------------------------------------------------------
unsigned Message::encode(f8String& to) const
{
	f8ostrstream msg;

#if defined CODECTIMING
	ostringstream gerr;
	gerr << "encode(" << _msgType << "):";
	IntervalTimer itm;
#endif

	if (!_header)
		throw MissingMessageComponent("header");
	Fields::const_iterator fitr(_header->_fields.find(Common_MsgType));
	static_cast<msg_type *>(fitr->second)->set(_msgType);
	_header->encode(msg);
	MessageBase::encode(msg);
	if (!_trailer)
		throw MissingMessageComponent("trailer");
	_trailer->encode(msg);
	const unsigned msgLen(msg.str().size());	// checksummable msglength

	f8ostrstream hmsg;
	if ((fitr = _header->_fields.find(Common_BeginString)) == _header->_fields.end())
		throw MissingMandatoryField(Common_BeginString);
	_header->_fp.clear(Common_BeginString, FieldTrait::suppress);
	fitr->second->encode(hmsg);
#if defined MSGRECYCLING
	_header->_fp.set(Common_BeginString, FieldTrait::suppress); // in case we want to reuse
#endif

	if ((fitr = _header->_fields.find(Common_BodyLength)) == _header->_fields.end())
		throw MissingMandatoryField(Common_BodyLength);
	_header->_fp.clear(Common_BodyLength, FieldTrait::suppress);
	static_cast<body_length *>(fitr->second)->set(msgLen);
	fitr->second->encode(hmsg);
#if defined MSGRECYCLING
	_header->_fp.set(Common_BodyLength, FieldTrait::suppress); // in case we want to reuse
#endif

	hmsg.write(msg.str().c_str(), msg.str().size());

	if ((fitr = _trailer->_fields.find(Common_CheckSum)) == _trailer->_fields.end())
		throw MissingMandatoryField(Common_CheckSum);
	static_cast<check_sum *>(fitr->second)->set(fmt_chksum(calc_chksum(hmsg.str())));
	_trailer->_fp.clear(Common_CheckSum, FieldTrait::suppress);
	fitr->second->encode(hmsg);
#if defined MSGRECYCLING
	_trailer->_fp.set(Common_CheckSum, FieldTrait::suppress); // in case we want to reuse
#endif

#if defined CODECTIMING
	gerr << itm.Calculate();
	GlobalLogger::log(gerr.str());
#endif

	to.assign(hmsg.str());
	return to.size();
}

//-------------------------------------------------------------------------------------------------
void MessageBase::print(ostream& os, int depth) const
{
	const string dspacer((depth + 1) * 3, ' ');
	const BaseMsgEntry *tbme(_ctx._bme.find_ptr(_msgType));
	if (tbme)
		os << tbme->_name << " (\"" << _msgType << "\")" << endl;
	for (Positions::const_iterator itr(_pos.begin()); itr != _pos.end(); ++itr)
	{
		const BaseEntry *tbe(_ctx._be.find_ptr(itr->second->_fnum));
		if (!tbe)
#if defined PERMIT_CUSTOM_FIELDS
			if (!_ctx._ube || (tbe = _ctx._ube->find_ptr(itr->second->_fnum)) == 0)
#endif
				throw InvalidField(itr->second->_fnum);
		os << dspacer << tbe->_name << " (" << itr->second->_fnum << "): ";
		int idx;
		if (itr->second->_rlm && (idx = (itr->second->get_rlm_idx())) >= 0)
			os << itr->second->_rlm->_descriptions[idx] << " (" << *itr->second << ')' << endl;
		else
			os << *itr->second << endl;
		if (_fp.is_group(itr->second->_fnum))
			print_group(itr->second->_fnum, os, depth);
	}
}

//-------------------------------------------------------------------------------------------------
void MessageBase::print_group(const unsigned short fnum, ostream& os, int depth) const
{
	const GroupBase *grpbase(find_group(fnum));
	if (!grpbase)
		throw InvalidRepeatingGroup(fnum);

	const string dspacer((depth + 1) * 3, ' ');
	size_t cnt(1);
	for (GroupElement::const_iterator itr(grpbase->_msgs.begin()); itr != grpbase->_msgs.end(); ++itr, ++cnt)
	{
		os << dspacer << (*itr)->_msgType << " (Repeating group " << cnt << '/' << grpbase->_msgs.size() << ')' << endl;
		(*itr)->print(os, depth + 1);
	}
}

//-------------------------------------------------------------------------------------------------
BaseField *MessageBase::replace(const unsigned short fnum, BaseField *with)
{
	BaseField *old(0);
	Fields::iterator itr(_fields.find(fnum));
	if (itr != _fields.end())
	{
		old = itr->second;
		unsigned pos(_fp.getPos(fnum));
		for (Positions::iterator pitr(_pos.begin()); pitr != _pos.end(); ++pitr)
		{
			if (pitr->second == old)
			{
				pos = pitr->first;
				_pos.erase(pitr);
				break;
			}
		}
		_pos.insert(Positions::value_type(pos, with));
		itr->second = with;
		_fp.set(fnum, FieldTrait::present);
	}
	return old;
}

//-------------------------------------------------------------------------------------------------
BaseField *MessageBase::replace(const unsigned short fnum, Presence::const_iterator fitr, BaseField *with)
{
	BaseField *old(0);
	Fields::iterator itr(_fields.find(fnum));
	if (itr != _fields.end())
	{
		old = itr->second;
		unsigned pos(_fp.getPos(fnum, fitr));
		for (Positions::iterator pitr(_pos.begin()); pitr != _pos.end(); ++pitr)
		{
			if (pitr->second == old)
			{
				pos = pitr->first;
				_pos.erase(pitr);
				break;
			}
		}
		_pos.insert(Positions::value_type(pos, with));
		itr->second = with;
		_fp.set(fnum, fitr, FieldTrait::present);
	}
	return old;
}

//-------------------------------------------------------------------------------------------------
BaseField *MessageBase::remove(const unsigned short fnum)
{
	BaseField *old(0);
	Fields::iterator itr(_fields.find(fnum));
	if (itr != _fields.end())
	{
		old = itr->second;
		for (Positions::iterator pitr(_pos.begin()); pitr != _pos.end(); ++pitr)
		{
			if (pitr->second == old)
			{
				_pos.erase(pitr);
				break;
			}
		}
		_fp.clear(fnum, FieldTrait::present);
		_fields.erase(itr);
	}
	return old;
}

//-------------------------------------------------------------------------------------------------
BaseField *MessageBase::remove(const unsigned short fnum, Presence::const_iterator fitr)
{
	BaseField *old(0);
	Fields::iterator itr(_fields.find(fnum));
	if (itr != _fields.end())
	{
		old = itr->second;
		for (Positions::iterator pitr(_pos.begin()); pitr != _pos.end(); ++pitr)
		{
			if (pitr->second == old)
			{
				_pos.erase(pitr);
				break;
			}
		}
		_fp.clear(fnum, fitr, FieldTrait::present);
		_fields.erase(itr);
	}
	return old;
}

//-------------------------------------------------------------------------------------------------
Message *Message::clone() const
{
	const BaseMsgEntry& bme(_ctx._bme.find_ref(_msgType));
	Message *msg(bme._create());
	copy_legal(msg, true);
	_header->copy_legal(msg->_header, true);
	_trailer->copy_legal(msg->_trailer, true);
	return msg;
}

//-------------------------------------------------------------------------------------------------
void Message::print(ostream& os) const
{
	if (_header)
		os << *_header;
	else
		os << "Null Header" << endl;
	MessageBase::print(os);
	if (_trailer)
		os << *_trailer;
	else
		os << "Null Trailer" << endl;
}

