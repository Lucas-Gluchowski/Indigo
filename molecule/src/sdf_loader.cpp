/****************************************************************************
 * Copyright (C) 2009-2015 EPAM Systems
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#include "molecule/sdf_loader.h"
#include "base_cpp/output.h"
#include "base_cpp/scanner.h"
#include "gzip/gzip_scanner.h"

using namespace indigo;

IMPL_ERROR(SdfLoader, "SDF loader");

CP_DEF(SdfLoader);

SdfLoader::SdfLoader (Scanner &scanner) :
CP_INIT,
TL_CP_GET(data),
TL_CP_GET(properties),
TL_CP_GET(_offsets),
TL_CP_GET(_preread)
{
   data.clear();
   properties.clear();

   // detect if input is gzipped
   byte id[2];
   int pos = scanner.tell();

   scanner.readCharsFix(2, (char *)id);
   scanner.seek(pos, SEEK_SET);

   if (id[0] == 0x1f && id[1] == 0x8b)
   {
      _scanner = new GZipScanner(scanner);
      _own_scanner = true;
   }
   else
   {
      _scanner = &scanner;
      _own_scanner = false;
   }
   _current_number = 0;
   _max_offset = 0;
   _offsets.clear();
   _preread.clear();
}

SdfLoader::~SdfLoader()
{
   if (_own_scanner)
      delete _scanner;
}

int SdfLoader::tell ()
{
   return _scanner->tell();
}

int SdfLoader::currentNumber ()
{
   return _current_number;
}

int SdfLoader::count ()
{
   int offset = _scanner->tell();
   int cn = _current_number;

   if (offset != _max_offset)
   {
      _scanner->seek(_max_offset, SEEK_SET);
      _preread.clear();
      _current_number = _offsets.size();
   }

   while (!isEOF())
      readNext();

   int res = _current_number;

   if (res != cn)
   {
      _scanner->seek(offset, SEEK_SET);
      _preread.clear();
      _current_number = cn;
   }

   return res;
}

bool SdfLoader::isEOF()
{
   // read space characters
   while (!_scanner->isEOF())
   {
      if (isspace(_scanner->lookNext()))
         _preread.push(_scanner->readChar());
      else
         return false;
   }

   // We are at the end of file now, having only space characters read.
   return true;
}

void SdfLoader::readNext ()
{
   ArrayOutput output(data);
   output.writeArray(_preread);
   int n_preread = _preread.size();
   _preread.clear();
   QS_DEF(Array<char>, str);

   if (_scanner->isEOF())
      throw Error("end of stream");

   _offsets.expand(_current_number + 1);
   _offsets[_current_number++] = _scanner->tell() - n_preread;

   properties.clear();

   bool pending_emptyline = false;

   int last_offset = -1;
   while (!_scanner->isEOF())
   {
      last_offset = _scanner->tell();
      _scanner->readLine(str, true);
      if (str.size() > 0 && str[0] == '>')
         break;
      if (str.size() > 3 && strncmp(str.ptr(), "$$$$", 4) == 0)
         break;
      if (pending_emptyline)
         output.printf("\n");
      if (str.size() <= 1)
         pending_emptyline = true;
      else
         pending_emptyline = false;

      if (!pending_emptyline)
         output.writeStringCR(str.ptr());

      if(data.size() > MAX_DATA_SIZE)
         throw Error("data size exceeded the acceptable size %d bytes, Please check for correct file format", MAX_DATA_SIZE);
   }
   
   int properties_offset = last_offset;

   while (1)
   {
      if (strncmp(str.ptr(), "$$$$", 4) == 0)
         break;

      output.writeStringCR(str.ptr());

      BufferScanner ws(str.ptr());

      while (!ws.isEOF())
         if (ws.readChar() == '<')
            break;

      QS_DEF(Array<char>, word);
      bool have_word = false;

      word.clear();

      while (!ws.isEOF())
      {
         char c = ws.readChar();

         if (c == '>')
         {
            have_word = true;
            break;
         }
         word.push(c);
      }

      if (have_word && word.size() > 0)
      {
         word.push(0);


         _scanner->readLine(str, true);
         auto& propBuf = properties.insert(word.ptr());
//         auto& propBuf = properties.valueBuf(word.ptr());
//         int idx = properties.findOrInsert(word.ptr());
         propBuf.copy(str);
         output.writeStringCR(str.ptr());
         if (str.size() > 1)
         {
            do
            {
               if (_scanner->isEOF())
                  break;

               _scanner->readLine(str, true);
               output.writeStringCR(str.ptr());
               if (str.size() > 1)
               {
                  propBuf.pop(); // Remove string end marker (0)
                  propBuf.push('\n');
                  propBuf.appendString(str.ptr(), true);
               }
            } while (str.size() > 1);
         }
      }

      if (_scanner->isEOF())
         break;

      _scanner->readLine(str, true);
   }

   if (_scanner->tell() > _max_offset)
      _max_offset = _scanner->tell();
}

void SdfLoader::readAt (int index)
{
   if (index < _offsets.size())
   {
      _scanner->seek(_offsets[index], SEEK_SET);
      _preread.clear();
      _current_number = index;
      readNext();
   }
   else
   {
      _scanner->seek(_max_offset, SEEK_SET);
      _preread.clear();
      _current_number = _offsets.size();
      do
      {
         readNext();
      } while (index + 1 != _offsets.size());
   }
}
